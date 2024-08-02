#include"xcpfs.h"
// #include"data.h"
static struct nat_entry *__lookup_nat(struct super_block *sb, int nid) ;

//return locked page and ref++
static struct page *read_raw_nat_block(struct super_block *sb, block_t iblock,bool for_write) {
    DEBUG_AT;
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct inode *meta_inode = sbi->meta_inode;
    struct page *page;
    
    page = xcpfs_prepare_page(meta_inode,iblock,for_write,true);
    return page;
}

static int __insert_nat(struct super_block *sb, int nid, int ino, int blkaddr, bool pinned, bool dirty) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_nat_info *nm = sbi->nm;
    struct nat_entry *ne = kmalloc(sizeof(struct nat_entry),GFP_KERNEL);
    struct nat_entry *t;
    ne->nid = nid;
    ne->ino = ino;
    ne->block_addr = blkaddr;
    ne->pinned = pinned;
    ne->dirty = false;
    INIT_LIST_HEAD(&ne->nat_link);
    //如果已经有了
    t = __lookup_nat(sb,nid);
    if(t) {
        XCPFS_INFO("nat exists:nid:%d",nid);
        kfree(ne);
        return -EEXIST;
    }

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
        kfree(ne);
    }
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
//在缓存中查找
static struct nat_entry *__lookup_nat(struct super_block *sb, int nid) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_nat_info *nm = sbi->nm;
    struct nat_entry *ne;
    struct list_head *it;
    int ret = -ENOKEY;
    // XCPFS_INFO("nat list begin");
    list_for_each(it,&nm->nat_list) {
        ne = container_of(it,struct nat_entry,nat_link);
        // XCPFS_INFO("nid:%d addr:0x%x",ne->nid,ne->block_addr);
        if(ne->nid == nid) {
            return ne;
        }
    }
    list_for_each(it,&nm->free_nat) {
        ne = container_of(it,struct nat_entry,nat_link);
        // XCPFS_INFO("nid:%d addr:0x%x",ne->nid,ne->block_addr);
        if(ne->nid == nid) {
            return ne;
        }
    }
    // XCPFS_INFO("nat list end");
    return NULL;
}
//现在缓存中查找，如果没有，那么读盘
struct nat_entry *lookup_nat(struct super_block *sb, int nid) {
    XCPFS_INFO("nid:%d",nid);
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_nat_info *nm = sbi->nm;
    struct nat_entry *ne;
    struct page *page;
    struct xcpfs_nat_block *raw_nats;
    struct xcpfs_nat_entry *raw_ne;
    block_t iblock = 0;
    int i;
    int retried = 0;
retry:
    DEBUG_AT;
    xcpfs_down_read(&nm->nat_info_rwsem);
    ne = __lookup_nat(sb,nid);
    xcpfs_up_read(&nm->nat_info_rwsem);
    DEBUG_AT;
    if(ne || retried) {
        if(ne == NULL) {
            return ne;
        }
        // if(ne && ne->block_addr == 0 || !ne) {
        //     XCPFS_INFO("nid free:addr:0x%x",ne?ne->block_addr:0xdeadbeef);
        //     return NULL;
        // }
        XCPFS_INFO("nid:%d addr:0x%x",ne->nid,ne->block_addr);
        return ne;
    }
    iblock = nid / NAT_ENTRY_PER_BLOCK;
    page = read_raw_nat_block(sb,iblock,false);
    raw_nats = (struct xcpfs_nat_block *)page_address(page);
    XCPFS_INFO("insert start");
    for(i = 0; i < NAT_ENTRY_PER_BLOCK; i++) {
        raw_ne = &raw_nats->entries[i];
        if(iblock * NAT_ENTRY_PER_BLOCK + i < 3) continue;
        XCPFS_INFO("nid:%d addr:0x%x",iblock * NAT_ENTRY_PER_BLOCK + i,raw_ne->block_addr);
        insert_nat(sb, iblock * NAT_ENTRY_PER_BLOCK + i, raw_ne->ino, raw_ne->block_addr, false, false);
    }
    XCPFS_INFO("insert end");
    unlock_page(page);
    put_page(page);
    retried = 1;
    goto retry;
}

int invalidate_nat(struct super_block *sb, int nid) {
    struct nat_entry *ne = lookup_nat(sb,nid);
    if(ne == NULL) {
        return -ENOENT;
    }
    ne->dirty = true;
    ne->ino = 0;
    ne->block_addr = 0;
    ne->pinned = false;
    return 0;
}

//TODO TODO 将meta node分开
static struct nat_entry *free_nat_empty(struct xcpfs_nat_info *nm,bool is_meta) {
    bool ret = 0;
    struct nat_entry *ne;
    xcpfs_down_read(&nm->nat_info_rwsem);
    list_for_each_entry(ne,&nm->free_nat,nat_link) {
        if(is_meta && ne->nid >= REG_NAT_START * NAT_ENTRY_PER_BLOCK ||
                  !is_meta && ne->nid < REG_NAT_START * NAT_ENTRY_PER_BLOCK) {
            continue;
        }
        list_del(&ne->nat_link);
        list_add(&ne->nat_link,&nm->nat_list);
        xcpfs_up_read(&nm->nat_info_rwsem);
        return ne;
    }
    xcpfs_up_read(&nm->nat_info_rwsem);
    return NULL;
}

struct nat_entry *alloc_free_nat(struct super_block *sb,bool is_meta) {
    DEBUG_AT;
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_nat_info *nm = sbi->nm;
    struct nat_entry *ne;
    struct xcpfs_nat_block *raw_nats;
    struct xcpfs_nat_entry *raw_ne;
    struct page *page;
    int iblock = 0;
    int i;
    
    if(!is_meta) {
        iblock = REG_NAT_START;
    }

    while(!(ne = free_nat_empty(nm,is_meta))) {
        page = read_raw_nat_block(sb,iblock,false);
        raw_nats = (struct xcpfs_nat_block *)page_address(page);
        XCPFS_INFO("find free nat begin");
        for(i = 0; i < NAT_ENTRY_PER_BLOCK; i++) {
            if(iblock * NAT_ENTRY_PER_BLOCK + i < 3) continue;
            raw_ne = &raw_nats->entries[i];
            if(i == NAT_ENTRY_PER_BLOCK - 1) {
                XCPFS_INFO("end ne:nid:%d addr:0x%x",iblock * NAT_ENTRY_PER_BLOCK + i,raw_ne->block_addr);
            }
            insert_nat(sb, iblock * NAT_ENTRY_PER_BLOCK + i, raw_ne->ino, raw_ne->block_addr, false, false);
        }
        XCPFS_INFO("find free nat end");
        unlock_page(page);
        put_page(page);
        iblock ++;
    }
    // xcpfs_down_write(&nm->nat_info_rwsem);
    // ne = list_first_entry(&nm->free_nat,struct nat_entry,nat_link);
    // list_del(&ne->nat_link);
    // list_add(&ne->nat_link,&nm->nat_list);
    // xcpfs_up_write(&nm->nat_info_rwsem);
    XCPFS_INFO("alloc:nid:%d addr:0x%x",ne->nid,ne->block_addr);
    return ne;
}

int flush_nat(struct super_block *sb) {
    DEBUG_AT;
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_nat_info *nm = sbi->nm;
    struct nat_entry *ne, *it;
    int iblock,off;
    struct page *page;
    struct xcpfs_nat_block *raw_nats;
    struct xcpfs_nat_entry *raw_ne;
    list_for_each_entry_safe(ne,it,&nm->nat_list,nat_link) {
        if(ne->dirty == false) continue;
        XCPFS_INFO("flush nat:nid:%d addr:0x%x",ne->nid,ne->block_addr);
        iblock = ne->nid / NAT_ENTRY_PER_BLOCK;
        off = ne->nid % NAT_ENTRY_PER_BLOCK;
        page = read_raw_nat_block(sb,iblock,true);
        raw_nats = (struct xcpfs_nat_block *)page_address(page);
        raw_ne = &raw_nats->entries[off];
        raw_ne->block_addr = ne->block_addr;
        raw_ne->ino = ne->ino;
        ne->dirty = false;
        // if(!ne->pinned) {
        //     remove_nat(sb,ne->nid);
        // }
        xcpfs_commit_write(page,page_offset(page),PAGE_SIZE);
    }
    return 0;
}