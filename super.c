#include"xcpfs.h"

static struct inode *xcpfs_alloc_inode(struct super_block *sb) {
    struct xcpfs_inode_info *xi;
    xi = kzalloc(sizeof(struct xcpfs_inode_info),GFP_KERNEL);
    if(!xi) {
        return NULL;
    }
    inode_init_once(&xi->vfs_inode);

    return &xi->vfs_inode;
}

static void xcpfs_destroy_inode(struct inode *inode) {
    struct xcpfs_inode_info *xi = (struct xcpfs_inode_info *)inode;
    kfree(xi);
}

static int xcpfs_drop_inode(struct inode *inode) {
    return generic_drop_inode(inode);
}

static void xcpfs_evict_inode(struct inode *inode) {
    // struct xcpfs_inode_info *xi = (struct xcpfs_inode_info *)inode;
    if(inode->i_ino <= 2) {
        return;
    }
    truncate_inode_pages_final(&inode->i_data);
    sync_inode_metadata(inode,0);
    if(!inode->i_link) {
        inode->i_size = 0;
        xcpfs_truncate(inode);
    }
    invalidate_inode_buffers(inode);
    clear_inode(inode);
}

static int xcpfs_write_inode(struct inode *inode, struct writeback_control *wbc) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(inode->i_sb);
    struct page *page;
    if(inode->i_ino == 2) {
        return 0;
    }
    xcpfs_update_inode_page(inode);
    page = get_node_page(inode->i_sb,inode->i_ino,false);
    write_single_page(page,wbc);
    return 0;
    //TODO:balance fs
}

static void flush_super_block(struct super_block *sb) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_nat_info *nm = sbi->nm;
    struct page *page;
    struct xcpfs_super_block *raw_super;
    struct xcpfs_zone_info *zone0,*zone1;
    int zone_id;

    page = alloc_page(GFP_KERNEL);
    raw_super = (struct xcpfs_super_block *)page_address(page);
    raw_super->magic = XCPFS_MAGIC;
    raw_super->meta_ino = 1;
    raw_super->node_ino = 2;
    raw_super->root_ino = 3;
    raw_super->nat_page_count = sbi->nat_page_count;
    raw_super->zit_page_count = sbi->zit_page_count;
    raw_super->ssa_page_count = sbi->ssa_page_count;

    zone0 = &zm->zone_info[0],zone1 = &zm->zone_info[1];
    if(zone0->cond == BLK_ZONE_COND_EMPTY) {
        zone_id = sector_to_block(zone1->wp);
    } else if(zone1->cond == BLK_ZONE_COND_EMPTY) {
        zone_id = sector_to_block(zone0->wp);
    } else if(zone0->wp > zone1->wp - zm->zone_size) {
        zone_id = sector_to_block(zone1->wp);
    } else {
        zone_id = sector_to_block(zone0->wp);
    }
    xcpfs_append_page(sb,page,zone_id);
    xcpfs_free_page(page);
}

//TODO:元数据的下刷
static void xcpfs_put_super(struct super_block* sb) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_nat_info *nm = sbi->nm;
    struct page *page;
    struct xcpfs_super_block *raw_super;
    struct xcpfs_zone_info *zone0,*zone1;
    int zone_id;

    do_checkpoint(sb);
    
    flush_super_block(sb);

    sb->s_fs_info = NULL;

    //free zm
    kfree(zm->zone_info);
    kfree(zm->zone_opened);
    kfree(zm->zone_active);
    kfree(zm);

    //free nm
    kfree(nm);

    kfree(sbi);
}
//just for debug
static int xcpfs_statfs(struct dentry* dentry, struct kstatfs* buf) {
    return -EIO;
}

const struct super_operations sfs_sops = {
    .alloc_inode = xcpfs_alloc_inode,
    .destroy_inode = xcpfs_destroy_inode,
    .write_inode = xcpfs_write_inode,
    .drop_inode = xcpfs_drop_inode,
    .evict_inode = xcpfs_evict_inode,
    .put_super = xcpfs_put_super,
    .statfs = xcpfs_statfs,
};