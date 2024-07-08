#include"xcpfs.h"

struct page *xcpfs_get_node_page(struct super_block *sb,nid_t nid) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct inode *node_inode = sbi->node_inode;
    struct xcpfs_io_info *xio;
    struct page *page;
    xio = alloc_xio();
    xio->ino = nid;
    xio->op = REQ_OP_READ;
    xio->type = META_NODE;
    
    submit_bio(xio);
    page = xio->page;
    lock_page(page);
    free_xio(xio);
    return page;
}