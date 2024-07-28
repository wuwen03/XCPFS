#include"xcpfs.h"

void init_cp_block(struct cp_block *cpsb, struct xcpfs_sb_info *sbi) {
    memset(cpsb,0,PAGE_SIZE);
    cpsb->cnt = 0;
    cpsb->raw_sb.magic = XCPFS_MAGIC;
    cpsb->raw_sb.meta_ino = sbi->meta_ino;
    cpsb->raw_sb.node_ino = sbi->node_ino;
    cpsb->raw_sb.root_ino = sbi->root_ino;
    cpsb->raw_sb.nat_page_count = sbi->nat_page_count;
    cpsb->raw_sb.zit_page_count = sbi->zit_page_count;
    cpsb->raw_sb.ssa_page_count = sbi->ssa_page_count;
}

#include"xcpfs.h"

void init_cp_block(struct cp_block *cpsb, struct xcpfs_sb_info *sbi) {
    memset(cpsb,0,PAGE_SIZE);
    cpsb->cnt = 0;
    cpsb->raw_sb.magic = XCPFS_MAGIC;
    cpsb->raw_sb.meta_ino = sbi->meta_ino;
    cpsb->raw_sb.node_ino = sbi->node_ino;
    cpsb->raw_sb.root_ino = sbi->root_ino;
    cpsb->raw_sb.nat_page_count = sbi->nat_page_count;
    cpsb->raw_sb.zit_page_count = sbi->zit_page_count;
    cpsb->raw_sb.ssa_page_count = sbi->ssa_page_count;
}

//将meta inode代表的元数据和超级块下刷
int do_checkpoint(struct super_block *sb) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct cp_block cp_b;
    struct page *page;

    if(!sbi->cpsb) {
        page = alloc_page(GFP_KERNEL);
        kmap(page);
        sbi->cpsb = (struct cp_block *)page_address(page);
        init_cp_block(sbi->cpsb,sbi);
    }

    xcpfs_down_write(&sbi->cp_sem);
    /*将所有data block落盘*/
    sbi->cp_phase = 0;
    sync_inodes_sb(sb);
    /*下刷reg node,meta data*/
    sbi->cp_phase = 1;
    filemap_write_and_wait_range(sbi->node_inode->i_mapping,REG_NAT_START * PAGE_SIZE,-1);
    filemap_write_and_wait_range(sbi->meta_inode->i_mapping,REG_NAT_START * NAT_ENTRY_SIZE,-1);
    filemap_write_and_wait_range(sbi->meta_inode->i_mapping,0,REG_NAT_START * NAT_ENTRY_SIZE - 1);

    xcpfs_up_write(&sbi->cp_sem);
    return 0;
}