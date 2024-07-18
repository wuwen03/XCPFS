#include"xcpfs.h"
//根据ino和iblock来获得page的类型
enum page_type get_page_type(struct xcpfs_sb_info *sbi,int ino, loff_t iblock) {
    if(ino == sbi->meta_ino) {
        return META_DATA;
    }
    if(ino == sbi->node_ino) {
        if(iblock < REG_NAT_START) {
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
    int i;
    for(i = 0; i < 4; i++) {
        offset[i] = -1;
    }
    //pointer in inode
    if(iblock < DEF_ADDRS_PER_INODE) {
        offset[0] = iblock;
        return 0;
    }
    //direct
    iblock -= DEF_ADDRS_PER_INODE;
    // i = (iblock + 1) / DEF_ADDRS_PER_BLOCK;
    i = roundup(iblock + 1, DEF_ADDRS_PER_BLOCK) - 1;
    if(i < 2) {
        offset[0] = DEF_ADDRS_PER_INODE + i;
        iblock -= i * DEF_ADDRS_PER_BLOCK;
        offset[1] = iblock % DEF_ADDRS_PER_BLOCK;
        return 1;
    }
    //indirect
    iblock -= 2 * DEF_ADDRS_PER_BLOCK;
    i = roundup(iblock + 1, DEF_NIDS_PER_BLOCK * DEF_ADDRS_PER_BLOCK) - 1;
    if(i < 2) {
        offset[0] = DEF_ADDRS_PER_INODE + 2 + i;
        iblock -= DEF_NIDS_PER_BLOCK * DEF_ADDRS_PER_BLOCK * i;
        offset[1] = roundup(iblock + 1, DEF_ADDRS_PER_BLOCK) - 1;
        iblock -= DEF_NIDS_PER_BLOCK * i;
        offset[2] = iblock;
        return 2;
    }
    //double indirect
    iblock -= 2 * DEF_NIDS_PER_BLOCK * DEF_ADDRS_PER_BLOCK;
    offset[0] = DEF_ADDRS_PER_INODE + 2 + 2;
    offset[1] = roundup(iblock + 1, DEF_NIDS_PER_BLOCK * DEF_ADDRS_PER_BLOCK) - 1;
    iblock -= offset[1] * DEF_NIDS_PER_BLOCK * DEF_ADDRS_PER_BLOCK;
    offset[2] = roundup(iblock + 1,DEF_ADDRS_PER_BLOCK) - 1;
    iblock -= offset[2] * DEF_ADDRS_PER_BLOCK;
    offset[3] = iblock;
    return 3;
}

/*return locked page and ref++*/
struct page *get_node_page(struct super_block *sb,nid_t nid,bool create) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct inode *node_inode = sbi->node_inode;
    // struct xcpfs_io_info *xio;
    // struct page *page;
    // xio = alloc_xio();
    // xio->sbi = sbi;
    // xio->ino = sbi->node_ino;
    // xio->iblock = nid;
    // xio->op = REQ_OP_READ;
    // // xio->type = REG_NODE;
    // xio->type = get_page_type(sbi,xio->ino,xio->iblock);
    // xio->create = create;
    // xio->checkpoint = false;
    // xio->pagep = &page;
    
    // xcpfs_submit_xio(xio);
    // lock_page(page);
    struct page *page;
    page = xcpfs_prepare_page(node_inode,nid,false,create);
    return page;
}

/*
page: data page
return locked page
*/
struct page *get_dnode_page(struct page *page,bool create, bool *need) {
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
    struct xcpfs_node *node;

    int iblock = page_index(page);
    len = get_path(offset,iblock);
    if(need) {
        *need = false;
    }
    next_nid = inode->i_ino;
    for(i = 0; i < 4; i++) {
        pages[i] = get_node_page(sb, next_nid, create);
        if(!pages[i] || PTR_ERR(pages[i])) {
            return pages[i];
        }
        if(i == len) {
            node = (struct xcpfs_node *)page_address(pages[i]);
            if(node->dn.addr[offset[i]] == 0) {
                if(!create){
                    unlock_page(pages[i]);
                    put_page(pages[i]);
                    return NULL;
                }
                if(need) {
                    *need = true;
                }
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
        if(next_nid == 0) {
            if(!create) {
                put_page(pages[i]);
                return ERR_PTR(-EIO);
            }
            ne = alloc_free_nat(sb);
            lock_page(pages[i]);
            if(i == 0) {
                node->i.i_nid[offset[0] - DEF_ADDRS_PER_INODE] = ne->nid;
            } else {
                node->in.nid[offset[i]] = ne->nid;
            }//TODO
            // SetPageDirty(pages[i]);
            set_page_dirty(pages[i]);
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
}