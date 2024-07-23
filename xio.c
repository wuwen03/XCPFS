
#include"xcpfs.h"
#include"xio.h"

#include<linux/blk_types.h>

static void xcpfs_read_end_io(struct bio *bio) {
    struct xcpfs_io_info *xio = (struct xcpfs_io_info *)bio->bi_private;
    xio->bio = NULL;
    SetPageUptodate(xio->page);
    unlock_page(xio->page);
    put_page(xio->page);
    bio_put(bio);
    free_xio(xio);
}

static void xcpfs_write_end_io(struct bio *bio) {
    struct xcpfs_io_info *xio = (struct xcpfs_io_info *)bio->bi_private;
    struct xcpfs_sb_info *sbi = xio->sbi;
    struct super_block *sb = sbi->sb;
    struct inode *inode;
    struct page *page;
    struct xcpfs_node *node;
    int offset[4];
    int i;

    if(xio->type == META_NODE || xio->type == REG_NODE) {
        update_nat(sb,xio->ino,bio->bi_iter.bi_sector,false);
        if(xcpfs_rwsem_is_locked(&sbi->cp_sem) && sbi->cp_phase == 1) {
            
        }
    } else {
        //TODO
        page = get_dnode_page(xio->page,false,NULL);
        node = (struct xcpfs_node *)page_address(page);
        get_path(offset,page_index(xio->page));
        for(i = 0; i < 4; i++) {
            if(i < 3 && offset[i + 1] == -1) {
                if(i == 0) {
                    node->i.i_addr[offset[i]] = bio->bi_iter.bi_sector;
                } else {
                    node->dn.addr[offset[i]] = bio->bi_iter.bi_sector;
                }
                break;
            } else if(i == 3) {
                node->dn.addr[offset[i]] = bio->bi_iter.bi_sector;
            }
        }
        SetPageDirty(page);
        unlock_page(page);
        put_page(page);
    }
    validate_blkaddr(sb,bio->bi_iter.bi_sector >> PAGE_SECTORS_SHIFT);
    end_page_writeback(xio->page);
    ClearPageDirty(xio->page);
    if(xio->unlock) {
        unlock_page(xio->page);
        put_page(xio->page);
    }
    free_xio(xio);
    bio_put(bio);
}

struct xcpfs_io_info *alloc_xio(void) {
    struct xcpfs_io_info *xio;
    xio = (struct xcpfs_io_info *)kzalloc(sizeof(struct xcpfs_io_info),GFP_KERNEL);
    return xio;
}

void free_xio(struct xcpfs_io_info *xio) {
    kfree(xio);
}

static struct bio *__alloc_bio(struct xcpfs_io_info *xio) {
    struct bio *bio;
    bio = bio_alloc(xio->sbi->sb->s_bdev,1,xio->op | xio->op_flags,GFP_NOIO);
    bio_add_page(bio,xio->page,PAGE_SIZE,0);
    bio->bi_private = xio;
    xio->bio = bio;

    bio->bi_iter.bi_sector = xio->new_blkaddr << PAGE_SECTORS_SHIFT;
    if(xio->op == REQ_OP_READ) {
        bio->bi_end_io = xcpfs_read_end_io;
    } else {
        set_page_writeback(xio->page);
        bio->bi_end_io = xcpfs_write_end_io;
    }
    return bio;
}

static int submit_node_xio(struct xcpfs_io_info *xio) {
    struct xcpfs_sb_info *sbi = xio->sbi;
    struct inode *node_inode = sbi->node_inode;
    struct address_space *mapping = node_inode->i_mapping;
    struct page *page;
    struct bio *bio;
    struct nat_entry *ne;
    int ret;

    // if(!xio->page) {   
    //     if(xio->op == REQ_OP_READ) {
    //         page = grab_cache_page(mapping,xio->ino);
    //         if(PageUptodate(page)) {
    //             return page;
    //         }
    //     } else {
    //         page = grab_cache_page_write_begin(mapping,xio->ino);
    //     }
    //     xio->page = page;
    // }
    page = xio->page;
    if(page == NULL) {
        return -EIO;
    }

    ne = lookup_nat(sbi->sb,xio->ino);
    if(ne) {
        xio->old_blkaddr = ne->block_addr;
    }
    alloc_zone(xio);

    bio = __alloc_bio(xio);
    bio->bi_private = xio;
    submit_bio(bio);
    return 0;
}

//TODO
static int submit_data_xio(struct xcpfs_io_info *xio) {
    struct xcpfs_sb_info *sbi = xio->sbi;
    struct inode *inode = xcpfs_iget(sbi->sb,xio->ino);
    struct address_space *mapping = inode->i_mapping;
    struct page *page, *dpage;
    struct bio *bio;
    struct nat_entry *ne;
    int offset[5] = {-1,-1,-1,-1,-1};
    struct xcpfs_node *node;
    int ret;
    int i;
    int len;
    page = xio->page;
    if(page == NULL) {
        return PTR_ERR(-EIO);
    }
    //fill the old_blkaddr of xio
    len = get_path(offset,xio->iblock);
    dpage = get_dnode_page(page,false,NULL);
    if(IS_ERR_OR_NULL(dpage)) {
        return PTR_ERR(dpage);
    }
    node = (struct xcpfs_node *)page_address(dpage);
    if(len == 0) {
        xio->old_blkaddr = node->i.i_addr[offset[len]];
    } else {
        xio->old_blkaddr = node->dn.addr[offset[len]];
    }
    unlock_page(dpage);
    put_page(dpage);

    alloc_zone(xio);
    bio = __alloc_bio(xio);
    bio->bi_private = xio;
    submit_bio(bio);
    iput(inode);
    return 0;
}

int xcpfs_submit_xio(struct xcpfs_io_info *xio) {
    enum page_type type = xio->type;
    struct xcpfs_sb_info *sbi = xio->sbi;
    int ret;

    if(type == META_NODE || type == REG_NODE) {
        ret = submit_node_xio(xio);
    } else {
        ret = submit_data_xio(xio);
    }

    return ret;
}