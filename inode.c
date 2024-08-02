#include"xcpfs.h"

static int xcpfs_fill_inode(struct inode *inode,struct xcpfs_inode *ri) {
    inode->i_mode = ri->i_mode;
    i_uid_write(inode,ri->i_uid);
    i_gid_write(inode,ri->i_gid);
    set_nlink(inode,ri->i_links);
    inode->i_size = ri->i_size;
    
    inode->i_atime.tv_sec = le64_to_cpu(ri->i_atime);
	inode->i_ctime.tv_sec = le64_to_cpu(ri->i_ctime);
	inode->i_mtime.tv_sec = le64_to_cpu(ri->i_mtime);
	inode->i_atime.tv_nsec = le32_to_cpu(ri->i_atime_nsec);
	inode->i_ctime.tv_nsec = le32_to_cpu(ri->i_ctime_nsec);
	inode->i_mtime.tv_nsec = le32_to_cpu(ri->i_mtime_nsec);
	inode->i_generation = le32_to_cpu(ri->i_generation);
    return 0;
} 
//TODO
int xcpfs_set_inode(struct inode *inode) {
    nid_t ino = inode->i_ino;
    if(ino == 1) {
        inode->i_mapping->a_ops = &xcpfs_data_aops;
        // inode->i_op = &xcpfs_file_inode_operations;
        // inode->i_fop = &xcpfs_file_operations;
    } else if(ino == 2) {
        inode->i_mapping->a_ops = &xcpfs_data_aops;
        // inode->i_op = &xcpfs_file_inode_operations;
        // inode->i_fop = &xcpfs_file_operations;
    } else if(S_ISREG(inode->i_mode)) {
        inode->i_mapping->a_ops = &xcpfs_data_aops;
        inode->i_op = &xcpfs_file_inode_operations;
        inode->i_fop = &xcpfs_file_operations;
    } else if(S_ISDIR(inode->i_mode)) {
        inode->i_mapping->a_ops = &xcpfs_data_aops;
        inode->i_op = &xcpfs_dir_inode_operations;
        inode->i_fop = &xcpfs_dir_operations;
    }
    return 0;
}

struct inode *xcpfs_iget(struct super_block *sb, nid_t ino) {
    DEBUG_AT;
    XCPFS_INFO("get ino:%d",ino);
    struct inode *inode;
    struct xcpfs_node *node;
    struct xcpfs_inode *ri;
    struct page *page;
    inode = iget_locked(sb,ino);
    if(!inode) {
        return ERR_PTR(-ENOMEM);
    }
    if(!(inode->i_state & I_NEW)) {
        return inode;
    }
    if(inode->i_ino == 2) {
        XCPFS_INFO("get node inode");
        goto out;
    }

    page = get_node_page(sb,ino,false);
    if(IS_ERR_OR_NULL(page)) {
        iget_failed(inode);
        return ERR_PTR(-EIO);
    }
    node = (struct xcpfs_node *)page_address(page);
    ri = &node->i;

    xcpfs_fill_inode(inode,ri);
    unlock_page(page);
    put_page(page);
out:
    xcpfs_set_inode(inode);
    unlock_new_inode(inode);
    return inode;
}

static int xcpfs_read_data_folio(struct file *file, struct folio *folio) {
    DEBUG_AT;
    struct page *page = &folio->page;
    struct xcpfs_sb_info *sbi = XCPFS_SB(page->mapping->host->i_sb);
    int ret;
    ret = do_prepare_page(page,false);
    unlock_page(page);
    put_page(page);
    return ret;
}

static int xcpfs_write_data_page(struct page *page, struct writeback_control *wbc) {
    DEBUG_AT;
    XCPFS_INFO("wbc->sync_mode:%d",wbc->sync_mode);
    // struct inode *inode = page_file_mapping(page)->host;
    struct inode *inode = page->mapping->host;
    struct xcpfs_sb_info *sbi = XCPFS_SB(inode->i_sb);
    struct xcpfs_io_info *xio;
    struct folio *folio = page_folio(page);
    int ret = -EAGAIN, index = page_to_index(page);
    XCPFS_INFO("write data page:inode ino:%d page index:%d",inode->i_ino,page_to_index(page));
    // if(folio_test_dirty(folio) == false && wbc->sync_mode == WB_SYNC_NONE) {
    //     XCPFS_INFO("page not dirty");
    //     ret = 0;
    //     folio_redirty_for_writepage(wbc,folio);
    //     goto out;
    // }
    //对于node inode的meta node部分
    if(inode->i_ino == 2 && index < REG_NAT_START * NAT_ENTRY_PER_BLOCK &&
              sbi->cp_phase != 1 && wbc->sync_mode == WB_SYNC_NONE) {
        // ret = AOP_WRITEPAGE_ACTIVATE;
        ret = 0;
        folio_redirty_for_writepage(wbc,folio);
        goto out;
    }
    ret = write_single_page(page,wbc);
    wait_on_page_writeback(page);
    // wait_on_page_locked()
    SetPageUptodate(page);
    // put_page(page);
out:
    unlock_page(page);
    // ClearPageDirty(page);
    return ret;
}

static int xcpfs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, struct page **pagep, void **fsdata) 
{
    struct inode *inode = mapping->host;
    struct xcpfs_sb_info *sbi = XCPFS_SB(inode->i_sb);
    struct page *page = NULL;
    int ret = -EAGAIN;
    pgoff_t index = ((unsigned long long) pos) >> PAGE_SHIFT;
    XCPFS_INFO("inode ino:%d index:%d",inode->i_ino,index);
    page = xcpfs_prepare_page(inode,index,true,true);
    *pagep = page;
    return 0;
}

static int xcpfs_write_end(struct file *file,
			struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
    DEBUG_AT;
    return xcpfs_commit_write(page,pos,copied);
}

static bool xcpfs_dirty_folio(struct address_space *mapping, struct folio *folio) {
    DEBUG_AT;
    struct page *page = folio_page(folio,0);
    struct inode *inode = page->mapping->host;
    if(!folio_test_uptodate(folio)) {
        folio_mark_uptodate(folio);
    } 
    if(filemap_dirty_folio(mapping,folio)) {
        XCPFS_INFO("dirty inode inode:%d ind:%d success %d",inode->i_ino,page_to_index(page),PageDirty(page));
        return true;
    }
    XCPFS_INFO("dirty fail %d",PageDirty(folio_page(folio,0)));
    return false;
}

const struct address_space_operations xcpfs_data_aops = {
    .read_folio = xcpfs_read_data_folio,
    .writepage = xcpfs_write_data_page,
    .write_begin = xcpfs_write_begin,
    .write_end = xcpfs_write_end,
    .direct_IO = noop_direct_IO,
    .dirty_folio = xcpfs_dirty_folio,
};


//TODO:xcpfs_getattr
int xcpfs_getattr(struct mnt_idmap * idmap, const struct path* path,
    struct kstat* stat, u32 request_mask, unsigned int flags)
{
    struct super_block *sb = path->dentry->d_sb;
    struct inode *inode = d_inode(path->dentry);
    generic_fillattr(idmap,inode,stat);
    // generic_fillattr(idmap,request_mask,inode,stat);
    return 0;
}

int xcpfs_update_inode_page(struct inode *inode) {
    DEBUG_AT;
    struct super_block *sb = inode->i_sb;
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct page *page;
    struct xcpfs_node *node;
    struct xcpfs_inode *xi;

    page = xcpfs_prepare_page(sbi->node_inode,inode->i_ino,true,true);
    if(IS_ERR_OR_NULL(page)) {
        return PTR_ERR(page);
    }
    DEBUG_AT;
    node = (struct xcpfs_node *)page_address(page);
    xi = &node->i;
    xi->i_mode = inode->i_mode;
    xi->i_uid = i_uid_read(inode);
    xi->i_gid = i_gid_read(inode);
    xi->i_links = inode->i_nlink;
    xi->i_size = inode->i_size;
    xi->i_blocks = inode->i_blocks;
    xi->i_atime = inode->i_atime.tv_sec;
    // xi->i_ctime = inode->i_ctime.tv_sec;
    xi->i_mtime = inode->i_mtime.tv_sec;
    xi->i_atime_nsec = inode->i_atime.tv_nsec;
    // xi->i_ctime_nsec = inode->i_ctime.tv_nsec;
    xi->i_mtime_nsec = inode->i_mtime.tv_nsec;
    xi->i_generation = inode->i_generation;

    xcpfs_commit_write(page,page_offset(page),PAGE_SIZE);
    return 0;
}
//TODO:xcpfs_new_inode
struct inode *xcpfs_new_inode(struct mnt_idmap *idmap,const struct inode *dir, umode_t mode) {
    DEBUG_AT;
    struct inode *inode = new_inode(dir->i_sb);
    struct xcpfs_sb_info *sbi = XCPFS_SB(dir->i_sb);
    struct nat_entry *ne;
    nid_t ino;
    if(!inode) {
        return ERR_PTR(-ENOMEM);
    }
    ne = alloc_free_nat(dir->i_sb,false);
    ne->ino = ne->nid;
    ino = ne->nid;
    inode->i_ino = ino;
    inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
    // inode->i_mtime = inode->i_atime = current_time(inode);
    inode->i_blocks = 0;
    inode_init_owner(idmap,inode,dir,mode);
    // insert_inode_hash(inode);
    insert_inode_locked(inode);
    mark_inode_dirty(inode);
    return inode;
}