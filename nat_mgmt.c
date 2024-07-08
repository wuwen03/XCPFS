#include"xcpfs.h"
#include"data.h"
//return locked page & ref++
static struct page *read_raw_nat_block(struct super_block *sb, block_t iblock) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct inode *meta_inode = sbi->meta_inode;
    struct xcpfs_io_info *xio = alloc_xio();
    struct page *page;

    xio->sbi = sbi;
    xio->op = REQ_OP_READ;
    xio->ino = sbi->meta_ino;
    xio->iblock = iblock;
    xio->type = META_DATA;
    spin_lock_init(&xio->io_lock);

    xcpfs_submit_xio(xio);
    page = xio->page;
    lock_page(page);
    free_xio(xio);
    return page;
}

static int __insert_nat(struct super_block *sb, int nid, int blkaddr, bool pinned, bool dirty) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_nat_info *nm = sbi->nm;
    struct nat_entry *ne = kmalloc(sizeof(struct nat_entry),GFP_KERNEL);
    ne->ino = nid;
    ne->block_addr = blkaddr;
    ne->pinned = pinned;
    ne->dirty = false;
    INIT_LIST_HEAD(&ne->nat_link);
    list_add(&ne->nat_link,&nm->nat_list);
    nm->cached_nat_count++;
    return 0;
}

int insert_nat(struct super_block *sb, int nid, int blkaddr, bool pinned, bool dirty) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_nat_info *nm = sbi->nm;
    int ret = 0;
    xcpfs_down_write(&nm->nat_info_rwsem);
    ret = __insert_nat(sb,nid,blkaddr,pinned,dirty);
    xcpfs_up_write(&nm->nat_info_rwsem);
    return ret;
}

static int __remove_nat(struct super_block *sb, int nid) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_nat_info *nm = sbi->nm;
    struct nat_entry *ne;
    struct list_head *it;
    int ret = -ENOKEY;
    list_for_each(it,&nm->nat_list) {
        ne = container_of(it,struct nat_entry,nat_link);
        if(ne->ino == nid) {
            ret = 0;
            break;
        }
    }
    if(ret == 0) {
        list_del(&ne->nat_link);
        nm->cached_nat_count--;
    }
    kfree(ne);
    return ret;
}

int remove_nat(struct super_block *sb, int nid) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_nat_info *nm = sbi->nm;
    struct nat_entry *ne;
    struct list_head *it;
    int ret = -ENOKEY;
    xcpfs_down_write(&nm->nat_info_rwsem);
    ret = __remove_nat(sb,nid);
    xcpfs_up_write(&nm->nat_info_rwsem);
    return ret;
}

static int __update_nat(struct super_block *sb, int nid,int new_blkaddr,bool pinned) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_nat_info *nm = sbi->nm;
    struct nat_entry *ne;
    struct list_head *it;
    int ret = -ENOKEY;
    list_for_each(it,&nm->nat_list) {
        ne = container_of(it,struct nat_entry,nat_link);
        if(ne->ino == nid) {
            ret = 0;
            break;
        }
    }
    if(ret == 0 && ne->block_addr != new_blkaddr) {
        list_del(&ne->nat_link);
        list_add(&ne->nat_link,&nm->nat_list);
        ne->block_addr = new_blkaddr;
        ne->pinned = pinned;
        ne->dirty = true;
    }
    return ret;
}

int update_nat(struct super_block *sb, int nid,int new_blkaddr,bool pinned) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_nat_info *nm = sbi->nm;
    struct nat_entry *ne;
    struct list_head *it;
    int ret = -ENOKEY;
    xcpfs_down_write(&nm->nat_info_rwsem);
    ret = __update_nat(sb,nid,new_blkaddr,pinned);
    xcpfs_up_write(&nm->nat_info_rwsem);
    return ret;
}

static static struct nat_entry *__lookup_nat(struct super_block *sb, int nid) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_nat_info *nm = sbi->nm;
    struct nat_entry *ne;
    struct list_head *it;
    int ret = -ENOKEY;
    list_for_each(it,&nm->nat_list) {
        ne = container_of(it,struct nat_entry,nat_link);
        if(ne->ino == nid) {
            ret = 0;
            break;
        }
    }
    return (ret ? NULL : ne);
}

struct nat_entry *lookup_nat(struct super_block *sb, int nid) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_nat_info *nm = sbi->nm;
    struct nat_entry *ne;
    struct page *page;
    struct xcpfs_nat_block *raw_nats;
    block_t iblock = 0;
    int i;
    int ret = -ENOKEY;
retry:
    xcpfs_down_read(&nm->nat_info_rwsem);
    ne = __lookup_nat(sb,nid);
    xcpfs_up_read(&nm->nat_info_rwsem);
    if(ne) {
        return ne;
    }
    iblock = nid / NAT_ENTRY_PER_BLOCK;
    page = read_raw_nat_block(sb,iblock);
    raw_nats = (struct xcpfs_nat_block *)page_address(page);
    for(i = 0; i < NAT_ENTRY_PER_BLOCK; i++) {
        ne = raw_nats[i];
        insert_nat(sb,ne->ino,ne->block_addr,false,false);
    }
    unlock_page(page);
    put_page(page);
    goto retry;
}

/*
page: data page
return locked page
*/
struct page *get_dnode_page(struct page *page) {
    struct inode *inode = page->mapping->host;
    struct super_block *sb = inode ->i_sb;
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct inode *node_inode = sbi->node_inode;
    int iblock = page_index(page);
    if(iblock < DEF_ADDRS_PER_INODE) {
        return xcpfs_get_node_page(sb,inode->i_ino);
    }
    iblock -= DEF_ADDRS_PER_INODE;
    
}