#include"xcpfs.h"

int cp_append_nat(struct super_block *sb,struct nat_entry *ne) {
    DEBUG_AT;
    XCPFS_INFO("meta_nat:nid:%d ino:%d addr:%x",ne->nid,ne->ino,ne->block_addr);
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_cpc *cpc = sbi->cpc;

    struct xcpfs_nat_entry_sb *meta_nat = cpc->raw_sb->meta_nat;
    meta_nat[cpc->raw_sb->meta_nat_cnt].nid = ne->nid;
    meta_nat[cpc->raw_sb->meta_nat_cnt].ne.block_addr = ne->block_addr;
    meta_nat[cpc->raw_sb->meta_nat_cnt].ne.ino = ne->ino;
    cpc->raw_sb->meta_nat_cnt ++;
    return 0;
}

void init_cpsb(struct xcpfs_cpc *cpc, struct xcpfs_sb_info *sbi) {
    memset(cpc->raw_sb,0,PAGE_SIZE);
    cpc->nat_ptr = 0;
    cpc->raw_sb->magic = XCPFS_MAGIC;
    cpc->raw_sb->meta_ino = sbi->meta_ino;
    cpc->raw_sb->node_ino = sbi->node_ino;
    cpc->raw_sb->root_ino = sbi->root_ino;
    cpc->raw_sb->nat_page_count = sbi->nat_page_count;
    cpc->raw_sb->zit_page_count = sbi->zit_page_count;
    cpc->raw_sb->ssa_page_count = sbi->ssa_page_count;
    cpc->raw_sb->meta_nat_cnt= 0;
}

//将meta inode代表的元数据和超级块下刷
int do_checkpoint(struct super_block *sb) {
    DEBUG_AT;
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct page *page;

    if(sbi->cpc == NULL) {
        sbi->cpc= kmalloc(sizeof(struct xcpfs_cpc),GFP_KERNEL);
        page = alloc_page(GFP_KERNEL);
        lock_page(page);
        sbi->cpc->page = page;
        sbi->cpc->raw_sb = (struct xcpfs_super_block *)page_address(page);
        init_cpsb(sbi->cpc,sbi);
    }
restart:
    if(sbi->cpc->restart) {
        sbi->cpc->restart = false;
        memset(sbi->cpc->raw_sb->meta_nat,0,META_NAT_NR * sizeof(struct xcpfs_nat_entry_sb));
        sbi->cpc->raw_sb->meta_nat_cnt = 0;
    }

    //将缓存在内存中的nat写到page上

    xcpfs_down_write(&sbi->cp_sem);
    XCPFS_INFO("------------phase 1-------------");
    /*将所有data block落盘*/
    sbi->cp_phase = 1;
    flush_nat(sb);
    flush_zit(sb);
    sync_inodes_sb(sb);
    XCPFS_INFO("------------phase 1-------------");
    /*下刷reg node,meta data*/
    XCPFS_INFO("------------phase 2-------------");
    sbi->cp_phase = 1;
    filemap_write_and_wait_range(sbi->meta_inode->i_mapping,REG_NAT_START * PAGE_SIZE,LLONG_MAX);
    filemap_write_and_wait_range(sbi->meta_inode->i_mapping,0,LLONG_MAX);
    filemap_write_and_wait_range(sbi->node_inode->i_mapping,REG_NAT_START * NAT_ENTRY_PER_BLOCK * PAGE_SIZE,LLONG_MAX);
    filemap_write_and_wait_range(sbi->node_inode->i_mapping,0,REG_NAT_START * NAT_ENTRY_PER_BLOCK * PAGE_SIZE);

    XCPFS_INFO("------------phase 2-------------");
    XCPFS_INFO("------------phase 3-------------");
    cp_append_nat(sb,lookup_nat(sb,1));
    cp_append_nat(sb,lookup_nat(sb,3));
    xcpfs_up_write(&sbi->cp_sem);
    XCPFS_INFO("------------phase 3-------------");

    XCPFS_INFO("------------------check point end-------------");

    if(sbi->cpc->restart) {
        goto restart;
    }
    return 0;
}