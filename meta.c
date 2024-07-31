#include"xcpfs.h"
//根据ino和iblock来获得page的类型
enum page_type get_page_type(struct xcpfs_sb_info *sbi, int ino, loff_t iblock) {
    if(ino == sbi->meta_ino) {
        return META_DATA;
    }
    if(ino == sbi->node_ino) {
        if(iblock < REG_NAT_START * NAT_ENTRY_PER_BLOCK) {
            return META_NODE;
        }
        return REG_NODE;
    }
    return REG_DATA;
}
/*
根据iblock，在offset中给出达到这个data block的偏移量
return 一共需要几级寻址
*/
int get_path(int offset[4], int iblock) {
    int i,depth;
    for(i = 0; i < 4; i++) {
        offset[i] = -1;
    }
    //pointer in inode
    if(iblock < DEF_ADDRS_PER_INODE) {
        offset[0] = iblock;
        XCPFS_INFO("iblock:%d path off:%d,%d,%d,%d",iblock,offset[0],offset[1],offset[2],offset[3]);
        return 0;
    }
    //direct
    iblock -= DEF_ADDRS_PER_INODE;
    // i = (iblock + 1) / DEF_ADDRS_PER_BLOCK;
    i = DIV_ROUND_UP(iblock + 1, DEF_ADDRS_PER_BLOCK) - 1;
    if(i < 2) {
        offset[0] = DEF_ADDRS_PER_INODE + i;
        iblock -= i * DEF_ADDRS_PER_BLOCK;
        offset[1] = iblock % DEF_ADDRS_PER_BLOCK;
        XCPFS_INFO("iblock:%d path off:%d,%d,%d,%d",iblock,offset[0],offset[1],offset[2],offset[3]);
        return 1;
    }
    //indirect
    iblock -= 2 * DEF_ADDRS_PER_BLOCK;
    i = DIV_ROUND_UP(iblock + 1, DEF_NIDS_PER_BLOCK * DEF_ADDRS_PER_BLOCK) - 1;
    if(i < 2) {
        offset[0] = DEF_ADDRS_PER_INODE + 2 + i;
        iblock -= DEF_NIDS_PER_BLOCK * DEF_ADDRS_PER_BLOCK * i;
        i = DIV_ROUND_UP(iblock + 1, DEF_ADDRS_PER_BLOCK) - 1;
        offset[1] = i;
        iblock -= DEF_NIDS_PER_BLOCK * i;
        offset[2] = iblock;
        XCPFS_INFO("iblock:%d path off:%d,%d,%d,%d",iblock,offset[0],offset[1],offset[2],offset[3]);
        return 2;
    }
    //double indirect
    iblock -= 2 * DEF_NIDS_PER_BLOCK * DEF_ADDRS_PER_BLOCK;
    offset[0] = DEF_ADDRS_PER_INODE + 2 + 2;
    i = DIV_ROUND_UP(iblock + 1, DEF_NIDS_PER_BLOCK * DEF_ADDRS_PER_BLOCK) - 1;
    offset[1] = i;
    iblock -= offset[1] * DEF_NIDS_PER_BLOCK * DEF_ADDRS_PER_BLOCK;
    offset[2] = DIV_ROUND_UP(iblock + 1,DEF_ADDRS_PER_BLOCK) - 1;
    iblock -= offset[2] * DEF_ADDRS_PER_BLOCK;
    offset[3] = iblock;
    XCPFS_INFO("iblock:%d path off:%d,%d,%d,%d",iblock,offset[0],offset[1],offset[2],offset[3]);
    return 3;
}

/*return locked page and ref++*/
struct page *get_node_page(struct super_block *sb,nid_t nid,bool create) {
    XCPFS_INFO("nid:%d create:%d",nid,create);
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct inode *node_inode = sbi->node_inode;
    struct page *page;
    page = __prepare_page(node_inode,nid,false,create,false);
    return page;
}

/*
page: data page
return locked page
*/
struct page *get_dnode_page(struct page *page,bool create, bool *need) {
    DEBUG_AT;
    struct inode *inode = page->mapping->host;
    struct super_block *sb = inode ->i_sb;
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct inode *node_inode = sbi->node_inode;
    struct nat_entry *ne;
    struct page *pages[4];
    int offset[5] = {-1,-1,-1,-1,-1};
    int next_nid,pre_nid;
    int i;
    int len;
    bool exists;
    bool is_meta = false;
    struct xcpfs_node *node;

    int iblock = page_index(page);
    len = get_path(offset,iblock);
    if(inode->i_ino == 1) {
        is_meta = true;
    }
    if(need) {
        *need = false;
    }
    next_nid = inode->i_ino;
    for(i = 0; i < 4; i++) {
        XCPFS_INFO("next_nid : %d",next_nid);
        pages[i] = get_node_page(sb, next_nid, create);
        if(IS_ERR_OR_NULL(pages[i])) {
            return pages[i];
        }
        if(i == len) {
            node = (struct xcpfs_node *)page_address(pages[i]);
            if(i == 0) {
                exists = (node->i.i_addr[offset[i]] != 0);
            } else {
                exists = (node->dn.addr[offset[i]] != 0);
            }
            if(!exists) {
                // if(!create){
                //     unlock_page(pages[i]);
                //     put_page(pages[i]);
                //     return NULL;
                // }
                if(need) {
                    *need = true;
                }
            }
            if(exists) {
                XCPFS_INFO("exists");
            } else {
                XCPFS_INFO("not exists")
            }
            return pages[i];
        }
        node = (struct xcpfs_node *)page_address(pages[i]);
        if(i == 0) {
            next_nid = node->i.i_nid[offset[0] - DEF_ADDRS_PER_INODE];
        } else {
            next_nid = node->in.nid[offset[i]];
        }
        unlock_page(pages[i]);
        XCPFS_INFO("ino:%d depth:%d off:%d next_nid:%d",inode->i_ino,i,offset[i],next_nid);
        if(next_nid == 0) {
            if(!create) {
                put_page(pages[i]);
                return ERR_PTR(-EIO);
            }
            XCPFS_INFO("creating");
            ne = alloc_free_nat(sb,is_meta);
            lock_page(pages[i]);
            wait_for_stable_page(pages[i]);
            if(i == 0) {
                node->i.i_nid[offset[0] - DEF_ADDRS_PER_INODE] = ne->nid;
            } else {
                node->in.nid[offset[i]] = ne->nid;
            }//TODO
            set_page_dirty(pages[i]);
            // SetPageDirty(pages[i]);
            SetPageUptodate(pages[i]);
            unlock_page(pages[i]);
            ne->ino = inode->i_ino;
            ne->block_addr = 0;
            ne->dirty = true;
            ne->pinned = false;
            next_nid = ne->nid;
        }
        pre_nid = next_nid;
        put_page(pages[i]);
    }
    return ERR_PTR(-EIO);
}