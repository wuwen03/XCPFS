#include"xcpfs.h"

static struct inode *xcpfs_alloc_inode(struct super_block *sb) {

}

static void xcpfs_free_inode(struct inode *) {

}

const struct super_operations sfs_sops = {
    .alloc_inode = xcpfs_alloc_inode,
    .free_inode = xcpfs_free_inode,
    
}