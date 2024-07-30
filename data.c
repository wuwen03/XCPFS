
#include"xcpfs.h"

#include<linux/blk_types.h>

/*
return locked page, and the page should be freed by xcpfs_free_page
these function are only used in IO of superblock
*/
struct page* xcpfs_grab_page(struct super_block *sb, block_t block) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_zm_info *zm = sbi->zm;
    struct page *page;
    struct bio_vec bio_vec;
    struct bio bio;
    int ret;

    XCPFS_INFO("block:%d",block);
    page = alloc_page(GFP_KERNEL);
    if (!page) {
        return -ENOMEM;
    }
    bio_init(&bio,sb->s_bdev,&bio_vec,1,REQ_OP_READ);
    bio.bi_iter.bi_sector = block << PAGE_SECTORS_SHIFT;
    __bio_add_page(&bio,page,PAGE_SIZE,0);
    ret = submit_bio_wait(&bio);
    XCPFS_INFO("ret:%d",ret);
    if (ret) {
        goto free_page;
    }
    lock_page(page);
    return page;
free_page:
    __free_page(page);
    return ERR_PTR(ret);
}

int xcpfs_append_page(struct super_block *sb, struct page *page, int zone_id) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_zm_info *zm = sbi->zm;
    struct bio_vec bio_vec;
    struct bio bio;
    int ret;
    bio_init(&bio,sb->s_bdev,&bio_vec,1,REQ_OP_ZONE_APPEND);
    bio.bi_iter.bi_sector = zm->zone_info[zone_id].start;
    __bio_add_page(&bio,page,PAGE_SIZE,0);
    ret = submit_bio_wait(&bio);
    XCPFS_INFO("status:%d addr:0x%x",bio.bi_status,bio.bi_iter.bi_sector);
    if(ret) {
        goto free_page;
    }
    return 0;
free_page:
    __free_page(page);
    return ret;
}

void xcpfs_free_page(struct page *page) {
    DEBUG_AT;
    if(IS_ERR_OR_NULL(page)) {
        return;
    }
    unlock_page(page);
    __free_page(page);
}

/*
para:page:locked page and ref has been increased
return:!(unlocked page with decreased ref)
*/
int do_prepare_page(struct page *page, bool create) {
    DEBUG_AT;
    struct inode *inode = page_file_mapping(page)->host;
    struct xcpfs_sb_info *sbi = XCPFS_SB(inode->i_sb);
    struct xcpfs_io_info *xio;
    struct page *dpage;
    int index = page_index(page);
    bool need;

    if(PageUptodate(page)) {
        return 0;
    }

    if(inode->i_ino == 2) {
        /*如果是对于node的准备，没有对应的dnode，又因为不是uptodate的page，所以就读盘*/
        XCPFS_INFO("read node page:nid:%d",index)
        goto read_disk;
    }
    /*对于data block的读取，首先要检查是否是要新创建，如果是新创建的，那么不用读盘，否则需要读盘*/
    dpage = get_dnode_page(page,create,&need); //设定create的时候不一定实际需要create
    if(IS_ERR_OR_NULL(dpage)) {
        XCPFS_INFO("get dnode page fail ino:%d data page ind:%d",inode->i_ino,page_to_index(page));
        return -EIO;
    }
    unlock_page(dpage);
    put_page(dpage);
    XCPFS_INFO("ino:%d,index:%d,create:%d,need:%d",inode->i_ino,index,create,need)
    if(IS_ERR_OR_NULL(dpage)) {
        return -EIO;
    } else if(need) {
        XCPFS_INFO("need create");
        zero_user_segment(page,0,PAGE_SIZE);
        SetPageUptodate(page);
        // lock_page(page);
        // get_page(page);
    } else {
        XCPFS_INFO("read disk");
    read_disk:
        /*此处表明mapping里面的page不是uptodate的，且盘上有相应的data block，则读盘*/
        xio = alloc_xio();
        xio->sbi = sbi;
        xio->ino = inode->i_ino;
        xio->iblock = index;
        xio->op = REQ_OP_READ;
        xio->op_flags = REQ_SYNC | REQ_PRIO;
        xio->type = get_page_type(sbi,inode->i_ino,index);
        xio->page = page;
        xio->unlock = true;
        xcpfs_submit_xio(xio);
        lock_page(page);
        get_page(page);
    }
    
    return 0;
}

struct page *__prepare_page(struct inode *inode, pgoff_t index, bool for_write, bool create, bool lock) {
    XCPFS_INFO("inode ino:%d index:%d ,forwrite:%d ,create:%d",inode->i_ino,index,for_write,create);
    struct xcpfs_sb_info *sbi = XCPFS_SB(inode->i_sb);
    struct page *page ,dpage;
    bool need;
    int ret;
    if(lock) {
        xcpfs_down_read(&sbi->cp_sem);
    }
    if(for_write) {
        page = pagecache_get_page(&inode->i_data,index,FGP_LOCK | FGP_WRITE | FGP_CREAT, GFP_NOFS);
        if(PageWriteback(page)) {
            wait_for_stable_page(page);
        }
    } else {
        page = grab_cache_page(&inode->i_data,index);
    }
    if(PageUptodate(page)) {
        if(lock) {
            xcpfs_up_read(&sbi->cp_sem);
        }
        return page;
    }
    ret = do_prepare_page(page,create);
    if (ret) {
        if(lock) {
            xcpfs_up_read(&sbi->cp_sem);
        }
        unlock_page(page);
        put_page(page);
        return ERR_PTR(ret);
    }
    if(!for_write && lock) {
        xcpfs_up_read(&sbi->cp_sem);
    }
    return page;
}

/*
return locked page with increased reference or -EIO
如果for_write,则得到一个uptodate and write begin的page，如果需要create，那么就create
*/
struct page *xcpfs_prepare_page(struct inode *inode, pgoff_t index, bool for_write, bool create) {
    return __prepare_page(inode,index,for_write,create,false);
}

int __commit_write(struct page *page, int pos, int copied, bool locked) {
    struct inode *inode = page->mapping->host;
    struct xcpfs_sb_info *sbi = XCPFS_SB(inode->i_sb);
    XCPFS_INFO("inode ino:%d pos:0x%x copied:%x",inode->i_ino,pos,copied);
    if(!PageUptodate(page)) {
        SetPageUptodate(page);
    }
    // SetPageDirty(page);
    set_page_dirty(page);
    if(inode->i_ino != 2) {
        if(pos + copied > i_size_read(inode)) {
            i_size_write(inode,pos+copied);
            mark_inode_dirty_sync(inode);
        }
    } else {
        mark_inode_dirty_sync(inode);
    }
    unlock_page(page);
    put_page(page);
    if(locked) {
        xcpfs_up_read(&sbi->cp_sem);
    }
    return copied;
}

//unlock page and ref --
int xcpfs_commit_write(struct page *page, int pos, int copied) {
    return __commit_write(page,pos,copied,false);
}

int xcpfs_commit_meta_write(struct page *page, int pos, int copied) {
    return __commit_write(page,pos,copied,false);
}

int do_write_single_page(struct page *page,struct writeback_control *wbc) {
    struct xcpfs_io_info *xio;
    struct inode *inode = page->mapping->host;
    int ret;
    XCPFS_INFO("inode ino:%d iblock:%d",inode->i_ino,page_index(page));
    xio = alloc_xio();
    xio->sbi = XCPFS_SB(inode->i_sb);
    xio->ino = inode->i_ino;
    xio->iblock = page_index(page);
    xio->op = REQ_OP_ZONE_APPEND;
    xio->op_flags = wbc ? wbc_to_write_flags(wbc) : 0;
    xio->type = get_page_type(xio->sbi,xio->ino,xio->iblock);
    xio->page = page;
    // xio->unlock = true;
    ret= xcpfs_submit_xio(xio);
    return ret;
}

/*page:locked page
return:the page is still locked (and need put_page)
*/
int write_single_page(struct page *page, struct writeback_control *wbc) {
    DEBUG_AT;
    XCPFS_INFO("PageWriteback:%d PageUptodate:%d PageLocked:%d",PageWriteback(page),PageUptodate(page),PageLocked(page));
    // struct inode *inode = page_file_mapping(page)->host;
    // struct xcpfs_sb_info *sbi = XCPFS_SB(inode->i_sb);
    int ret;
    if(PageWriteback(page)) {
        wait_on_page_writeback(page);
    }
    set_page_writeback(page);
    
    XCPFS_INFO("after set page write back")
    ret = do_write_single_page(page,wbc);
    return ret;
}