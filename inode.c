#include"xcpfs.h"

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
    node = (struct xcpfs_node *)page_address(page);
    ri = node->i;
    //TODO
}