#include"xcpfs.h"
#include"data.h"
//return locked page and ref++
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
    xio->create = true;
    spin_lock_init(&xio->io_lock);

    xcpfs_submit_xio(xio);
    page = xio->page;
    lock_page(page);
    free_xio(xio);
    return page;
}

static int __insert_nat(struct super_block *sb, int nid, int ino, int blkaddr, bool pinned, bool dirty) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_nat_info *nm = sbi->nm;
    struct nat_entry *ne = kmalloc(sizeof(struct nat_entry),GFP_KERNEL);
    ne->nid = nid;
    ne->ino = ino;
    ne->block_addr = blkaddr;
    ne->pinned = pinned;
    ne->dirty = false;
    INIT_LIST_HEAD(&ne->nat_link);
    if(ne->block_addr){
        list_add(&ne->nat_link,&nm->nat_list);
    } else {
        list_add(&ne->nat_link,&nm->free_nat);
    }
    nm->cached_nat_count++;
    return 0;
}

int insert_nat(struct super_block *sb, int nid, int ino, int blkaddr, bool pinned, bool dirty) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_nat_info *nm = sbi->nm;
    int ret = 0;
    xcpfs_down_write(&nm->nat_info_rwsem);
    ret = __insert_nat(sb,nid,ino,blkaddr,pinned,dirty);
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
        if(ne->nid == nid) {
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
        if(ne->nid == nid) {
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
        if(ne->ni == nid) {
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

static bool free_nat_empty(struct xcpfs_nat_info *nm) {
    int ret = 0;
    xcpfs_down_read(&nm->nat_info_rwsem);
    ret = list_empty(&nm->free_nat);
    xcpfs_up_read(&nm->nat_info_rwsem);
    return ret;
}

struct nat_entry *alloc_free_nat(struct super_block *sb) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_nat_info *nm = sbi->nm;
    struct nat_entry *ne;
    struct xcpfs_nat_block *raw_nats;
    struct page *page;
    int iblock = 0;
    int i;

    while(free_nat_empty(nm)) {
        page = read_raw_nat_block(sb,iblock++);
        raw_nats = (struct xcpfs_nat_block *)page_address(page);
        for(i = 0; i < NAT_ENTRY_PER_BLOCK; i++) {
            ne = raw_nats[i];
            insert_nat(sb,ne->ino,ne->block_addr,false,false);
        }
        unlock_page(page);
        put_page(page);
    }
    xcpfs_down_write(&nm->nat_info_rwsem);
    ne = list_first_entry(&nm->free_nat,struct nat_entry,nat_link);
    list_del(&ne->nat_link);
    list_add(&nm->nat_list,&ne->nat_link);
    xcpfs_up_write(&nm->nat_info_rwsem);
    return ne;
}