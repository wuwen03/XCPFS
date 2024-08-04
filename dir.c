#include"xcpfs.h"

static int xcpfs_readdir(struct file *file, struct dir_context *ctx) {
    struct inode *inode = file_inode(file);
    struct super_block *sb = inode->i_sb;
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct page *page;
    int npages = dir_pages(inode);
    int i,offset;
    int pos = ctx->pos;

    const char *name;
    int len;
    nid_t ino;
    struct xcpfs_dentry *de;
    struct inode *next_inode;

    ctx->pos = pos = ALIGN(pos,PAGE_SIZE);
    for(i = pos; i < npages; i++) {
        page = xcpfs_prepare_page(inode,i,false,false);
        if(IS_ERR(page)) {
            continue;
        }
        de = (struct xcpfs_dentry *)page_address(page);
        name =de->name;
        ino = de->ino;
        len = strnlen(name,XCPFS_MAX_FNAME_LEN);
        if(ino == 0) {
            goto next;
        }
        next_inode = xcpfs_iget(sb,ino);
        if(!dir_emit(ctx,name,len,ino,fs_umode_to_dtype(next_inode->i_mode))) {
            iput(next_inode);
            unlock_page(page);
            put_page(page);
            return 0;
        }
        iput(next_inode);
    next:
        ctx->pos += PAGE_SIZE;
        unlock_page(page);
        put_page(page);
    }
    return 0;
}

const struct file_operations xcpfs_dir_operations = {
    .llseek = generic_file_llseek,
    .read = generic_read_dir,
    .iterate_shared = xcpfs_readdir,
    .fsync = generic_file_fsync
};

static int xcpfs_add_link(struct dentry *dentry, struct inode *inode) {
    DEBUG_AT;
    struct inode *dir = d_inode(dentry->d_parent);
    struct xcpfs_dentry *de;
    struct page *page;
    int npages = dir_pages(dir);
    const char *name = dentry->d_name.name;
    int namelen = dentry->d_name.len;
    int i;
    int tar = npages;
    for(i = 0; i < npages; i++) {
        page = xcpfs_prepare_page(dir,i,false,false);
        if(IS_ERR(page)) {
            return PTR_ERR(page);
        }
        de = (struct xcpfs_dentry *)page_address(page);
        if(de->ino == 0 && tar == npages) {
            tar = i;
        }
        if(namelen == de->namelen && strcmp(name,de->name) == 0) {
            unlock_page(page);
            put_page(page);
            return -EEXIST;
        }
        unlock_page(page);
        put_page(page);
    }
    page = xcpfs_prepare_page(dir,tar,true,true);
    if(IS_ERR(page)) {
        return PTR_ERR(page);
    }
    de = (struct xcpfs_dentry *)page_address(page);
    de->ino = inode->i_ino;
    // memcpy(de->name,name,namelen);
    strcpy(de->name,name);
    de->namelen = namelen;
    de->ino = inode->i_ino;
    xcpfs_commit_write(page,page_offset(page),PAGE_SIZE);
    dir->i_mtime = dir->i_atime = current_time(dir);
    mark_inode_dirty(dir);
    return 0;
}

static int xcpfs_mknod(struct mnt_idmap* idmap, struct inode* dir,
    struct dentry* dentry, umode_t mode, dev_t rdev) 
{
    DEBUG_AT;
    struct inode *inode;
    int err;

    if(!old_valid_dev(rdev)) {
        return -EINVAL;
    }
    inode = xcpfs_new_inode(idmap,dir,mode);
    if(IS_ERR(inode)) {
        return PTR_ERR(inode);
    }
    xcpfs_set_inode(inode);
    mark_inode_dirty(inode);
    xcpfs_down_read(&(XCPFS_SB(inode->i_sb)->cp_sem));
    err = xcpfs_add_link(dentry,inode);
    xcpfs_up_read(&(XCPFS_SB(inode->i_sb)->cp_sem));
    if(!err) {
        d_instantiate_new(dentry,inode);
        return 0;
    }
    XCPFS_INFO("mknod fail");
    inode_dec_link_count(inode);
    iput(inode);
    return err;
}

static int xcpfs_create(struct mnt_idmap* idmap, struct inode* dir,
    struct dentry* dentry, umode_t mode, bool excl) {
    DEBUG_AT;
    return xcpfs_mknod(idmap, dir, dentry, mode, 0);
}

//return de and locked and referred res_page
static struct xcpfs_dentry *xcpfs_find_entry(struct dentry* dentry,struct page **res_page) {
    DEBUG_AT;
    struct page *page;
    struct inode *dir = d_inode(dentry->d_parent);
    struct xcpfs_dentry *de;
    const char *name = dentry->d_name.name;
    int namelen = dentry->d_name.len;
    int nrpages = dir_pages(dir);
    int i;
    for (i = 0; i < nrpages; i++) {
        page = xcpfs_prepare_page(dir,i,false,false);
        if(IS_ERR_OR_NULL(page)) {
            continue;
        }
        de = (struct xcpfs_dentry *)page_address(page);
        XCPFS_INFO("de->ino:%d de->name:%s",de->ino,de->name);
        if(de->ino == 0) {
            unlock_page(page);
            put_page(page);
            continue;
        }
        if(de->namelen == namelen && strcmp(de->name,name) == 0) {
            *res_page = page;
            return de;
        }
        unlock_page(page);
        put_page(page);
    }
    return NULL;
} 

static uint32_t xcpfs_inode_by_name(struct dentry* dentry) {
    DEBUG_AT;
    struct page* page;
    struct xcpfs_dentry *de = xcpfs_find_entry(dentry,&page);
    int res;
    if (de) {
        res = de->ino;
        unlock_page(page);
        put_page(page);
        return res;
    }
    return 0;
}

static struct dentry* xcpfs_lookup(struct inode* dir, struct dentry* dentry,
    unsigned int flags)
{
    DEBUG_AT;
    struct inode *inode = NULL;
    int ino;
    if(dentry->d_name.len > XCPFS_MAX_FNAME_LEN) {
        return ERR_PTR(-ENAMETOOLONG);
    }
    ino = xcpfs_inode_by_name(dentry);
    if(ino) {
        inode = xcpfs_iget(dir->i_sb,ino);
    }
    XCPFS_INFO("looked up inode : %d",ino);
    return d_splice_alias(inode,dentry);
}

static int xcpfs_init_dir(struct inode* inode, struct inode* dir) {
    DEBUG_AT;
    struct sfs_sb_info* sbi = inode->i_sb->s_fs_info;
    struct xcpfs_dentry *de;
    struct page *page;
    int i;
    int err;
    for (i = 0;i < 2;i++) {
        page = xcpfs_prepare_page(inode,i,true,true);
        if (!page) {
            return -ENOMEM;
        }
        if(IS_ERR(page)) {
            // unlock_page(page);
            // put_page(page);
            return PTR_ERR(page);
        }
        de = (struct xcpfs_dentry *)page_address(page);
        de->ino = inode->i_ino;
        if (i == 0) {
            strcpy(de->name, ".");
            de->namelen = strlen(".");
        } else {
            strcpy(de->name, "..");
            de->namelen = strlen("..");
        }
        xcpfs_commit_write(page,page_offset(page),PAGE_SIZE);
    }
    return 0;
}

static int xcpfs_mkdir(struct mnt_idmap* idmap, struct inode* dir, 
    struct dentry* dentry, umode_t mode)
{
    DEBUG_AT;
    struct inode *inode ;
    int err;
    inode = xcpfs_new_inode(idmap,dir,mode | S_IFDIR);
    if(IS_ERR(inode)) {
        return PTR_ERR(inode);
    }
    inode_inc_link_count(dir);
    inode_inc_link_count(inode);
    xcpfs_set_inode(inode);
    err = xcpfs_init_dir(inode,dir);
    if(err) {
        goto fail;
    }
    xcpfs_down_read(&XCPFS_SB(inode->i_sb)->cp_sem);
    err = xcpfs_add_link(dentry,inode);
    xcpfs_up_read(&XCPFS_SB(inode->i_sb)->cp_sem);
    if(err) {
        goto fail;
    }
    d_instantiate_new(dentry,inode);
    return err;
fail:
    inode_dec_link_count(inode);
    inode_dec_link_count(inode);
    iput(inode);
    return 0;
}
/*
page:locked page
effect:unlock page*/
static int xcpfs_delete_entry(struct xcpfs_dentry* de, struct page* page) {
    XCPFS_INFO("de ptr:%p page ptr:%p",de,page);
    DEBUG_AT;
    int index;
    // struct xcpfs_dentry *de;
    struct inode *inode;
    index = page_index(page);
    unlock_page(page);
    page = xcpfs_prepare_page(page->mapping->host,index,true,false);
    if(IS_ERR_OR_NULL(page)) {
        return -EIO;
    }
    de = (struct xcpfs_dentry *)page_address(page);
    XCPFS_INFO("ino:%d name:%s",de->ino,de->name);
    de->ino = 0;
    de->namelen = 0;
    memset(de->name,0,XCPFS_MAX_FNAME_LEN);
    xcpfs_commit_write(page,page_offset(page),PAGE_SIZE);
    inode = page->mapping->host;
    inode->i_mtime = current_time(inode);
    mark_inode_dirty(inode);
    return 0;
}

static int xcpfs_unlink(struct inode* dir, struct dentry* dentry) {
    DEBUG_AT;
    XCPFS_INFO("dir ptr:%p,dentry:%p",dir,dentry);
    int err = -ENOENT;
    struct xcpfs_dentry *de;
    struct page *page;
    struct inode *inode;
    de = xcpfs_find_entry(dentry,&page);
    if(!de) {
        XCPFS_INFO("debug1")
        return -ENOENT;
    }
    XCPFS_INFO("debug2");
    xcpfs_down_read(&(XCPFS_SB(dir->i_sb)->cp_sem));
    err = xcpfs_delete_entry(de,page);
    xcpfs_up_read(&(XCPFS_SB(dir->i_sb)->cp_sem));
    put_page(page);
    if(err) {
        return err;
    }
    inode = d_inode(dentry);
    inode->i_atime = dir->i_atime;
    inode_dec_link_count(inode);
    mark_inode_dirty(inode);
    return err;
}

static int xcpfs_empty_dir(struct inode *inode) {
    struct page *page;
    struct xcpfs_dentry *de;
    int npages = dir_pages(inode);
    int i;
    if(npages <= 2) {
        return 1;
    }
    for (i = 2; i < npages; i++) {
        page = xcpfs_prepare_page(inode,i,false,false);
        if(IS_ERR_OR_NULL(page)) {
            continue;
        }
        de = (struct xcpfs_dentry *)page_address(page);
        if(de->ino != 0) {
            unlock_page(page);
            put_page(page);
            return 0;
        }
        unlock_page(page);
        put_page(page);
    }
    return 1;
}

static int xcpfs_rmdir(struct inode* dir, struct dentry* dentry) {
    DEBUG_AT;
    struct inode *inode = d_inode(dentry);
    int err = -ENOTEMPTY;
    if(!xcpfs_empty_dir(inode)) {
        return err;
    }
    xcpfs_down_read(&XCPFS_SB(inode->i_sb)->cp_sem);
    err = xcpfs_unlink(dir,dentry);
    xcpfs_up_read(&XCPFS_SB(inode->i_sb)->cp_sem);
    if(err) {
        return err;
    }
    inode_dec_link_count(dir);
    inode_dec_link_count(inode);
    return err;
}


const struct inode_operations xcpfs_dir_inode_operations = {
    .mknod = xcpfs_mknod,
    .create = xcpfs_create,
    .lookup = xcpfs_lookup,
    .mkdir = xcpfs_mkdir,
    .unlink = xcpfs_unlink,
    .rmdir = xcpfs_rmdir,
};