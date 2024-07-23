//TODO:xcpfs_truncate
#include"xcpfs.h"

static int xcpfs_recurse(struct inode *inode, int offset[4] ,int depth, int nid) {
    struct super_block *sb = inode->i_sb;
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct page *page;
    struct xcpfs_node *node;
    struct direct_node *dn = NULL;
    struct indirect_node *in = NULL;
    int len = 0;
    int i;

    for(i = 0; i < 4; i++) {
        if(offset[i] == -1) {
            break;
        }
        len ++;
    }
    page = get_node_page(sb,nid,false);
    node = (struct xcpfs_node *)page_address(page);
    if(depth == 0) {
        dn = &node->dn;
    } else {
        in = &node->in;
    }

    i = (offset == NULL ? 0 : offset[len - depth - 1]);
    for(;i < DEF_NIDS_PER_BLOCK; i++) {
        if(depth == 0) {
            if(dn->addr[i]) {
                invalidate_blkaddr(sb,dn->addr[i]);
                dn->addr[i] = 0;
            }
            continue;
        }        
        if(in->nid[i]) {
            xcpfs_recurse(inode,offset,depth - 1, in->nid[i]);
            invalidate_nat(sb,in->nid[i]);
            in->nid[i] = 0;
        }
    }
    if(offset && depth == 0) {
        offset[len - 1] --;
    }
    if(offset && depth != 0 && offset[len - depth - 1 + 1] == 0) {
        invalidate_nat(sb,in->nid[offset[len - depth - 1]]);
        in->nid[i] = 0;
        offset[len - depth - 1] --;
    }
    if(!offset || offset[len - depth - 1] == 0) {
        ClearPageUptodate(page);
    } else {
        set_page_dirty(page);
    }
    unlock_page(page);
    put_page(page);
    return 0;
}

static int xcpfs_truncate_blocks(struct inode *inode, struct page *ipage, pgoff_t free_from) {
    struct super_block *sb = inode->i_sb;
    int offset[5] = {-1,-1,-1,-1,-1};
    int i,len;
    struct xcpfs_node *node;
    struct xcpfs_inode *ri;
    bool flag = false;

    len = get_path(offset,free_from);
    node = (struct xcpfs_node *)page_address(ipage);
    ri = &node->i;
    
    if(offset[0] < DEF_ADDRS_PER_INODE) {
        for (i = offset[0];i < DEF_ADDRS_PER_INODE; i++) {
            if(ri->i_addr[i]) {
                invalidate_blkaddr(sb,ri->i_addr[i]);
                ri->i_addr[i] = 0;
            }
        }
        offset[0] = DEF_ADDRS_PER_INODE;
        flag = true;
    }
    if(offset[0] < DEF_ADDRS_PER_INODE + 2) {
        for(i = offset[0] - DEF_ADDRS_PER_INODE; i < 2; i++) {
            if(ri->i_nid[i]) {
                xcpfs_recurse(inode,flag?NULL:offset,0,ri->i_nid[i]);
                if(flag || offset[1] == 0) {
                    invalidate_nat(sb,ri->i_nid[i]);
                    ri->i_nid[i] = 0;
                }
            }
        }
        offset[0] = DEF_ADDRS_PER_INODE + 2;
        flag = true;
    }
    if(offset[0] < DEF_ADDRS_PER_INODE + 4) {
        for(i = offset[0] - DEF_ADDRS_PER_INODE; i < 4; i++) {
            if(ri->i_nid[i]) {
                xcpfs_recurse(inode,flag?NULL:offset,1,ri->i_nid[i]);
                if(flag || offset[1] == 0) {
                    invalidate_nat(sb,ri->i_nid[i]);
                    ri->i_nid[i] = 0;
                }
            }
        }
        offset[0] = DEF_ADDRS_PER_INODE + 4;
        flag = true;
    }
    if(offset[0] < DEF_ADDRS_PER_INODE + 5) {
        for(i = offset[0] - DEF_ADDRS_PER_INODE; i < 5; i++) {
            if(ri->i_nid[i]) {
                xcpfs_recurse(inode,flag?NULL:offset,2,ri->i_nid[i]);
                if(flag || offset[1] == 0) {
                    invalidate_nat(sb,ri->i_nid[i]);
                    ri->i_nid[i] = 0;
                }
            }
        }
        offset[0] = DEF_ADDRS_PER_INODE + 6;
        flag = true;
    }
    set_page_dirty(ipage);
    return 0;
}

static int xcpfs_truncate_partial_block(struct inode *inode,pgoff_t iblock, int offset) {
    struct page *page;
    struct xcpfs_io_info *xio;
    int ret = -EAGAIN;

    if(offset == BLOCK_SIZE) {
        return 0;
    }

    page = xcpfs_prepare_page(inode,iblock,true,false);
    if(IS_ERR_OR_NULL(page)) {
        return -EIO;
    }
    zero_user(page,offset,PAGE_SIZE - offset);
    xcpfs_commit_write(page,page_offset(page),PAGE_SIZE);
    return 0;
}

static int xcpfs_do_truncate(struct inode *inode, int from, bool lock) {
    struct super_block *sb = inode->i_sb;
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    pgoff_t free_from,iblock;
    int offset;
    struct page *ipage;

    free_from = (pgoff_t)((from + BLOCK_SIZE - 1) >> BLOCK_SIZE_BITS);

    if(lock) {
        xcpfs_down_read(&sbi->cp_sem);
    }

    ipage = get_node_page(sb,inode->i_ino,false);
    if(IS_ERR_OR_NULL(ipage)) {
        return PTR_ERR(ipage);
    }

    xcpfs_truncate_blocks(inode,ipage,free_from);
    
    unlock_page(ipage);
    put_page(ipage);
//TODO:truncate partial block
    iblock = (pgoff_t)(from >> BLOCK_SIZE_BITS);
    offset = from - (iblock << BLOCK_SIZE_BITS);
    xcpfs_truncate_partial_block(inode,iblock,offset);

    if(lock) {
        xcpfs_up_read(&sbi->cp_sem);
    }
    return 0;
}

int xcpfs_truncate(struct inode *inode) {
    int err;

    err = xcpfs_do_truncate(inode,i_size_read(inode),true);
    if(err) {
        return err;
    }
    inode->i_mtime = inode->i_ctime = current_time(inode);
    mark_inode_dirty_sync(inode);
    return 0;
}