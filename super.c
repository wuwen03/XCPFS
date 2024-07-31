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
    DEBUG_AT;
    XCPFS_INFO("inode ino:%d size:%d link:%d nlink:%d count:%d",inode->i_ino,inode->i_size,inode->i_link,inode->i_nlink,inode->i_count);
    // struct xcpfs_inode_info *xi = (struct xcpfs_inode_info *)inode;
    struct xcpfs_sb_info *sbi = XCPFS_SB(inode->i_sb);
    struct inode *node_inode = sbi->node_inode;
    // int i;
    if(inode->i_ino <= 2) {
        truncate_inode_pages_final(inode->i_mapping);
        clear_inode(inode);
        return;
    }
    truncate_inode_pages_final(inode->i_mapping);
    sync_inode_metadata(inode,0);
    if(!inode->i_nlink) {
        inode->i_size = 0;
        xcpfs_truncate(inode);
        invalidate_mapping_pages(node_inode->i_mapping,inode->i_ino,inode->i_ino);
    }
    invalidate_inode_buffers(inode);
    XCPFS_INFO("nrpages:%d",inode->i_data.nrpages);
    clear_inode(inode);
    XCPFS_INFO("inode state:%x I_FREEING|I_CLEAR:%x",inode->i_state,I_FREEING|I_CLEAR)
}

static int xcpfs_write_inode(struct inode *inode, struct writeback_control *wbc) {
    DEBUG_AT;
    XCPFS_INFO("inode ino:%d",inode->i_ino);
    XCPFS_INFO("wbc->sync_mode:%d",wbc->sync_mode);
    struct xcpfs_sb_info *sbi = XCPFS_SB(inode->i_sb);
    struct page *page;
    if(inode->i_ino == 2) {
        // ClearPageDirty(page);
        // SetPageUptodate(page);
        return 0;
    }
    xcpfs_update_inode_page(inode);
    page = get_node_page(inode->i_sb,inode->i_ino,false);
    if(IS_ERR_OR_NULL(page)) {
        XCPFS_INFO("fail to get inode page:ino %d",inode->i_ino);
        return -EIO;
    }
    if(wbc->sync_mode == WB_SYNC_NONE) {
        XCPFS_INFO("writing inode ino:%d page ptr:0x%p",inode->i_ino,page);
        XCPFS_INFO("page index:%d refcount:%d",page_to_index(page),page_ref_count(page));
        write_single_page(page,wbc);
        get_page(page);//为了实现方便
    }
    unlock_page(page);
    put_page(page);
    return 0;
    //TODO:balance fs
}

static void flush_super_block(struct super_block *sb) {
    DEBUG_AT;
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_nat_info *nm = sbi->nm;
    struct page *page;
    struct xcpfs_super_block *raw_super;
    struct xcpfs_zone_info *zone0,*zone1;
    int zone_id;

    // page = alloc_page(GFP_KERNEL);
    // raw_super = (struct xcpfs_super_block *)page_address(page);
    // raw_super->magic = XCPFS_MAGIC;
    // raw_super->meta_ino = 1;
    // raw_super->node_ino = 2;
    // raw_super->root_ino = 3;
    // raw_super->nat_page_count = sbi->nat_page_count;
    // raw_super->zit_page_count = sbi->zit_page_count;
    // raw_super->ssa_page_count = sbi->ssa_page_count;
    page = sbi->cpc->page;
    zone0 = &zm->zone_info[0],zone1 = &zm->zone_info[1];
    if(zone0->cond == BLK_ZONE_COND_EMPTY) {
        // zone_id = sector_to_block(zone1->wp);
        zone_id = 1;
    } else if(zone1->cond == BLK_ZONE_COND_EMPTY) {
        // zone_id = sector_to_block(zone0->wp);
        zone_id = 0;
    } else if(zone0->wp > zone1->wp - zm->zone_size) {
        // zone_id = sector_to_block(zone1->wp);
        zone_id = 1;
    } else {
        // zone_id = sector_to_block(zone0->wp);
        zone_id = 0;
    }
    xcpfs_append_page(sb,page,zone_id);
    xcpfs_free_page(page);
}

//TODO:元数据的下刷
static void xcpfs_put_super(struct super_block* sb) {
    DEBUG_AT;
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_nat_info *nm = sbi->nm;
    struct nat_entry *ne;
    struct page *page;
    struct xcpfs_super_block *raw_super;
    struct xcpfs_zone_info *zone0,*zone1;
    int zone_id,i;

    // do_checkpoint(sb);
    
    flush_super_block(sb);

    // iput(sbi->node_inode);

    dump_fs(sb);

    sb->s_fs_info = NULL;

    //free zm
    for(i = 0; i < zm->nr_zones; i++) {
        kfree(zm->zone_info->valid_map);
    }
    kfree(zm->zone_info);
    kfree(zm->zone_opened);
    kfree(zm->zone_active);
    kfree(zm);

    //free nm
    while(list_empty(&nm->nat_list)) {
        ne = list_first_entry(&nm->nat_list,struct nat_entry,nat_link);
        list_del(&ne->nat_link);
    }
    while(list_empty(&nm->free_nat)) {
        ne = list_first_entry(&nm->free_nat,struct nat_entry,nat_link);
        list_del(&ne->nat_link);
    }
    kfree(nm);

    if(sbi->cpc) {
        kfree(sbi->cpc);
    }
    kfree(sbi);
}

void dump_fs(struct super_block *sb) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi;
    struct nat_entry *ne;
    struct inode *inode;
    int i = 0;
    
    XCPFS_INFO("-------DUMP STATISTICS--------");
    XCPFS_INFO("-----nat info------");
    list_for_each_entry(ne,&sbi->nm->nat_list,nat_link) {
        XCPFS_INFO("nid:%d addr:0x%x ino:%d",ne->nid,ne->block_addr,ne->ino);
    }
    XCPFS_INFO("-----nat info------");
    XCPFS_INFO("----inode inf------")
    list_for_each_entry(inode,&sb->s_inodes,i_sb_list) {
        XCPFS_INFO("ino:%d i_nlink:%d i_count:%d nrpages:%d",
                        inode->i_ino,inode->i_nlink,inode->i_count,inode->i_mapping->nrpages);
    }
    XCPFS_INFO("----inode inf------");
    XCPFS_INFO("----zone info------");
    for(zi = zm->zone_active[i = 0]; i < zm->max_active_zones; zi = zm->zone_active[++i]) {
        if(zi == NULL) {
            continue;
        }
        XCPFS_INFO("zoneid:%d start:%x wp:%x cond:%d type:%d",zi->zone_id,zi->start,zi->wp,zi->cond,zi->zone_type);
    }
    XCPFS_INFO("----zone info------");
    XCPFS_INFO("-------DUMP STATISTICS--------");
}

//just for debug
static int xcpfs_statfs(struct dentry* dentry, struct kstatfs* buf) {
    struct super_block *sb = dentry->d_inode->i_sb;
    
    dump_fs(sb);
    return 0;
}

const struct super_operations xcpfs_sops = {
    .alloc_inode = xcpfs_alloc_inode,
    .destroy_inode = xcpfs_destroy_inode,
    .write_inode = xcpfs_write_inode,
    .drop_inode = xcpfs_drop_inode,
    .evict_inode = xcpfs_evict_inode,
    .put_super = xcpfs_put_super,
    .statfs = xcpfs_statfs,
};