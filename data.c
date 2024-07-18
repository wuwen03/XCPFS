
#include"xcpfs.h"

#include<linux/blk_types.h>

/*return locked page, and the page should be freed by xcpfs_free_page*/
struct page* xcpfs_grab_page(struct super_block *sb, block_t block) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_zm_info *zm = sbi->zm;
    struct page *page;
    struct bio_vec bio_vec;
    struct bio bio;
    int ret;

    page = alloc_page(GFP_KERNEL);
    if (!page) {
        return -ENOMEM;
    }
    bio_init(&bio,sb->s_bdev,&bio_vec,1,REQ_OP_READ);
    bio.bi_iter.bi_sector = block << PAGE_SECTORS_SHIFT;
    __bio_add_page(&bio,page,BLOCK_SIZE,0);
    ret = submit_bio_wait(&bio);
    if (ret) {
        goto free_page
    }
    lock_page(page);
    return page;
free_page:
    __free_page(page);
    return ret;
}

int xcpfs_append_page(struct super_block *sb, struct page *page, int zone_id) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_zm_info *zm = sbi->zm;
    struct bio_vec bio_vec;
    struct bio bio;
    int ret;
    bio_init(&bio,sb->s_bdev,&bio_vec,1,REQ_OP_ZONE_APPEND);
    bio.bi_iter.bi_sector = zm->zone_info[zone_id].start;
    __bio_add_page(&bio,page,BLOCK_SIZE,0);
    ret = subit_bio_wait(&bio);
    if(ret) {
        goto free_page
    }
free_page:
    __free_page(page);
    return ret;
}

void xcpfs_free_page(struct page *page) {
    __free_page(page);
}

/*
return locked page with reference++ or -EIO
如果for_write,则得到一个uptodate and write begin的page，如果需要create，那么就create
*/
struct page *xcpfs_prepare_page(struct inode *inode, pgoff_t index, bool for_write, bool create) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(inode->i_sb);
    struct page *page ,dpage;
    struct xcpfs_io_info *xio;
    bool need;
    int offset[5],len;
    if(for_write) {
        page = pagecache_get_page(&inode->i_data,index,FGP_LOCK | FGP_WRITE | FGP_CREAT, GFP_NOFS);
        if(PageWriteback(page)) {
            wait_for_stable_page(page);
        }
    } else {
        page = grab_cache_page(&inode->i_data,index);
    }
    if(PageUptodate(page)) {
        return page;
    }
    dpage = get_dnode_page(page,create,&need); //TODO:需要create的时候不一定实际需要create
    if(IS_ERR_OR_NULL(dpage)) {
        return PTR_ERR(-EIO);
    } else if(need) {
        zero_user_segment(page,0,BLOCK_SIZE);
        SetPageUptodate(page);
    } else {
        /*此处表明mapping里面不是最新的，且盘上有相应的data block，则读盘*/
        xio = alloc_xio();
        xio->sbi = sbi;
        xio->ino = inode->i_ino;
        xio->iblock = index;
        xio->op = REQ_OP_READ;
        xio->type = get_page_type(sbi,inode->i_ino,index);
        xio->create = false;
        xio->checkpoint = false;
        xio->page = page;
        xcpfs_submit_xio(xio);
        lock_page(page);
        get_page(page);
    }
    return page;
}

//unlock page and ref --
int xcpfs_commit_write(struct page *page, int pos, int copied) {
    struct inode *inode = page->mapping->host;
    if(!PageUptodate(page)) {
        SetPageUptodate(page);
    }
    set_page_dirty(page);
    if(inode->i_ino > 2) {
        if(pos + copied > i_size_read(inode)) {
            i_size_write(inode,pos+copied);
            mark_inode_dirty_sync(inode);
        }
    }
    unlock_page(page);
    put_page(page);
    return copied;
}