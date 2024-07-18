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
} 
//TODO
int xcpfs_set_inode(struct inode *inode) {
    nid_t ino = inode->i_ino;
    if(ino == 1) {
        
    } else if(ino == 2) {

    } else if(S_ISREG(inode->i_mode)) {
        inode->i_mapping->a_ops = &xcpfs_data_aops;
        inode->i_op = &xcpfs_file_inode_operations;
        inode->i_fop = &xcpfs_file_operations;
    } else if(S_ISDIR(inode->i_mode)) {
        inode->i_mapping->a_ops = &xcpfs_data_aops;
        inode->i_op = &xcpfs_dir_inode_operations;
        inode->i_fop = &xcpfs_dir_operations;
    }
}

struct inode *xcpfs_iget(struct super_block *sb, nid_t ino) {
    struct inode *inode;
    struct xcpfs_node *node;
    struct xcpfs_inode *ri;
    struct page *page;
    inode = iget_locked(sb,ino);
    if(!inode) {
        return PTR_ERR(-ENOMEM);
    }
    if(!(inode->i_state & I_NEW)) {
        return inode;
    }

    page = get_node_page(sb,ino,false);
    if(IS_ERR_OR_NULL(page)) {
        iget_failed(inode);
        return ERR_PTR(-EIO);
    }
    node = (struct xcpfs_node *)page_address(page);
    ri = node->i;

    xcpfs_fill_inode(inode,ri);
    unlock_page(page);
    put_page(page);
    xcpfs_set_inode(inode);
    unlock_new_inode(inode);
    return inode;
}

static int xcpfs_read_data_folio(struct file *file, struct folio *folio) {
    struct page *page = &folio->page;
    struct inode *inode = page_file_mapping(page)->host;
    struct xcpfs_io_info *xio;
    int ret = -EAGAIN;

    xio = alloc_xio();
    xio->sbi = inode->i_sb;
    xio->ino = inode->i_ino;
    xio->iblock = page_index(page);
    xio->op = REQ_OP_READ;
    xio->type = REG_DATA;
    xio->create = false;
    xio->checkpoint = false;
    xio->page = page;

    ret = xcpfs_submit_xio(xio);
    return ret;
}

static int xcpfs_write_data_page(struct page *page, struct writeback_control *wbc) {
    struct page *page = &folio->page;
    struct inode *inode = page_file_mapping(page)->host;
    struct xcpfs_io_info *xio;
    int ret = -EAGAIN;

    xio = alloc_xio();
    xio->sbi = XCPFS_SB(inode->i_sb);
    xio->ino = inode->i_ino;
    xio->iblock = page_index(page);
    xio->op = REQ_OP_ZONE_APPEND;
    // xio->type = xio->ino == 1? META_DATA: REG_DATA;
    xio->type = get_page_type(xio->sbi,xio->ino,xio->iblock);
    xio->create = false;
    xio->checkpoint = false;
    xio->page = page;
    xio->wbc = wbc;

    ret = xcpfs_submit_xio(xio);
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
    page = xcpfs_prepare_page(inode,index,true,true);
    *pagep = page;
    return 0;
}

static int xcpfs_write_end(struct file *file,
			struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
    return xcpfs_commit_write(page,pos,copied);
}

const struct address_space_operations xcpfs_data_aops = {
    .read_folio = xcpfs_read_data_folio,
    .writepage = xcpfs_write_data_page,
    .write_begin = xcpfs_write_begin,
    .write_end = xcpfs_write_end,
    .direct_IO = noop_direct_IO,
};

//TODO:xcpfs_getattr
int xcpfs_getattr(struct user_namespace* mnt_userns, const struct path* path,
    struct kstat* stat, u32 request_mask, unsigned int flags)
{
    struct super_block *sb = path->dentry->d_sb;
    struct inode *inode = d_inode(path->dentry);
    generic_fillattr(&init_user_ns,inode,stat);
    return 0;
}

void xcpfs_update_inode_page(struct inode *inode) {
    struct super_block *sb = inode->i_sb;
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct page *page;
    struct xcpfs_node *node;
    struct xcpfs_inode *xi;

    page = get_node_page(sb,inode->i_ino,false);
    if(!page) {
        return;
    }
    wait_on_page_writeback(page);
    set_page_dirty(page);
    node = (struct xcpfs_node *)page_address(page);
    xi = node->i;
    xi->i_mode = inode->i_mode;
    xi->i_uid = inode->i_uid;
    xi->i_gid = inode->i_gid;
    xi->i_links = inode->i_nlink;
    xi->i_size = inode->i_size;
    xi->i_blocks = inode->i_blocks;
    xi->i_atime = inode->i_atime.tv_sec;
    xi->i_ctime = inode->i_ctime.tv_sec;
    xi->i_mtime = inode->i_mtime.tv_sec;
    xi->i_atime_nsec = inode->i_atime.tv_nsec;
    xi->i_ctime_nsec = inode->i_ctime.tv_nsec;
    xi->i_mtime_nsec = inode->i_mtime.tv_nsec;
    xi->i_generation = inode->i_generation;

    unlock_page(page);
    put_page(page);
}
//TODO:xcpfs_new_inode
struct inode *xcpfs_new_inode(const struct inode *dir, umode_t mode) {
    struct inode *inode = new_inode(dir->i_sb);
    struct xcpfs_sb_info *sbi = XCPFS_INFO(dir->i_sb);
    struct nat_entry *ne;
    nid_t ino;
    if(!inode) {
        return ERR_PTR(-ENOMEM);
    }
    ne = alloc_free_nat(dir->i_sb,false);
    ino = ne->nid;
    inode->i_ino = ino;
    inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
    inode->i_blocks = 0;
    insert_inode_hash(inode);
    mark_inode_dirty(inode);
    return inode;
}