#include"xcpfs.h"

const struct file_operations xcpfs_file_operations = {
    .llseek = generic_file_llseek,
    .read_iter = generic_file_read_iter,
    .write_iter = generic_file_write_iter,
    .mmap = generic_file_mmap,
    .fsync = generic_file_fsync,

};

static int xcpfs_setattr(struct mnt_idmap* idmap,
    struct dentry* dentry, struct iattr* attr)
{
    struct inode *inode = d_inode(dentry);
    int error;

    XCPFS_INFO("start truncate inode:ino %d size:%d",inode->i_ino,inode->i_size);
    error = setattr_prepare(idmap,dentry,attr);
    if(error) {
        return error;
    }
    if ((attr->ia_valid & ATTR_SIZE) &&
                attr->ia_size != i_size_read(inode)) {
    	error = inode_newsize_ok(inode, attr->ia_size);
    	if (error)
    		return error;  

    	truncate_setsize(inode, attr->ia_size);
    	xcpfs_truncate(inode);
    }

    setattr_copy(idmap, inode, attr);
    mark_inode_dirty(inode);
    return 0;
}

const struct inode_operations xcpfs_file_inode_operations = {
    .setattr = xcpfs_setattr,
    .getattr = xcpfs_getattr,
};
