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

static void xcpfs_drop_inode(struct inode *inode) {
    return generic_drop_inode(inode);
}
//TODO
static void xcpfs_evict_inode(struct inode *inode) {
    struct xcpfs_inode_info *si = (struct xcpfs_inode_info *)inode;

}

static void xcpfs_put_super(struct super_block* sb) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;


}

static int xcpfs_statfs(struct dentry* dentry, struct kstatfs* buf) {
    return -EIO;
}

const struct super_operations sfs_sops = {
    .alloc_inode = xcpfs_alloc_inode,
    .destroy_inode = xcpfs_destroy_inode,
    .drop_inode = xcpfs_drop_inode,
    .evict_inode = xcpfs_evict_inode,
    .put_super = xcpfs_put_super,
    .statfs = xcpfs_statfs,
}