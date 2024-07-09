#include<linux/blk_types.h>

#include"xcpfs.h"
#include"data.h"


/*return locked page, and the page should be freed by xcpfs_free_page*/
struct page* xcpfs_grab_page(struct super_block *sb, block_t block) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_zm_info *zm = sbi->zm;
    struct page *page;
    struct bio_vec bio_vec;
    struct bio bio;
    int ret;

    page = alloc_page(GFP_KERNEL);
    if (!page) {
        return -ENOMEM;
    }
    bio_init(&bio,sb->s_bdev,&bio_vec,1,REQ_OP_READ);
    bio.bi_iter.bi_sector = block << PAGE_SECTORS_SHIFT;
    __bio_add_page(&bio,page,BLOCK_SIZE,0);
    ret = submit_bio_wait(&bio);
    if (ret) {
        goto free_page
    }
    lock_page(page);
    return page;
free_page:
    __free_page(page);
    return ret;
}

void xcpfs_free_page(struct page *page) {
    __free_page(page);
}

static void xcpfs_read_end_io(struct bio *bio) {
    struct xcpfs_io_info *xio = (struct xcpfs_io_info *)bio->bi_private;
    xio->bio = NULL;
    SetPageUptodate(xio->page);
    unlock_page(xio->page);
    put_page(xio->page);
    bio_put(bio);
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
    } else {
        //TODO
        page = get_dnode_page(xio->page,false);
        node = page_address(page);
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
    ClearPageDirty(xio->page);
    unlock_page(xio->page);
    put_page(xio->page);
    bio_put(bio);
}

struct xcpfs_io_info *alloc_xio() {
    struct xcpfs_io_info *xio;
    xio = (struct xcpfs_io_info *)kzalloc(sizeof(struct xcpfs_io_info),GFP_KERNEL);
    spin_lock_init(&xio->io_lock);
}

void free_xio(struct xcpfs_io_info *xio) {
    kfree(xio);
}

static struct bio *__alloc_bio(struct xcpfs_io_info *xio) {
    struct bio *bio;
    bio = bio_alloc(xio->sbi->sb->s_bdev,1,xio->op,GFP_NOIO);
    bio_add_page(bio,xio->page,PAGE_SIZE,0);
    bio->bi_private = xio;
    xio->bio = bio;

    bio->bi_iter.bi_sector = xio->new_blkaddr << PAGE_SECTORS_SHIFT;
    if(xio->op == REQ_OP_READ) {
        bio->bi_end_io = xcpfs_read_end_io;
    } else {
        bio->bi_end_io = xcpfs_write_end_io;
    }
}

static int submit_node_xio(struct xcpfs_io_info *xio) {
    struct xcpfs_sb_info *sbi = xio->sbi;
    struct inode *node_inode = sbi->node_inode;
    struct address_space *mapping = node_inode->i_mapping;
    struct page *page;
    struct bio *bio;
    struct nat_entry *ne;
    int ret;
    if(xio->op == REQ_OP_READ) {
        page = grab_cache_page(mapping,xio->ino);
    } else {
        page = grab_cache_page_write_begin(mapping,xio->ino);
    }
    xio->page = page;

    ne = lookup_nat(sbi->sb,xio->ino);
    xio->old_blkaddr = ne->block_addr;
    alloc_zone(xio);

    bio = __alloc_bio(xio);
    submit_bio(bio);
    return 0;
}
//TODO
static int submit_data_xio(struct xcpfs_io_info *xio) {
    struct xcpfs_sb_info *sbi = xio->sbi;
    struct inode *inode = xcpfs_iget(sbi->sb,xio->ino);
    struct address_space *mapping = inode->i_mapping;
    struct page *page;
    struct bio *bio;
    struct nat_entry *ne;
    int ret;
    if(xio->op == REQ_OP_READ) {
        page = grab_cache_page(mapping,xio->iblock);
    } else {
        page = grab_cache_page_write_begin(mapping,xio->iblock);
    }
    xio->page = page;
    alloc_zone(xio);

    bio = __alloc_bio(xio);
    submit_bio(bio);
    return 0;
}

int xcpfs_submit_xio(struct xcpfs_io_info *xio) {
    enum page_type type = xio->type;
    int ret;
    if(type == META_NODE || type == REG_NODE) {
        ret = submit_node_xio(xio);
    } else {
        ret = submit_data_xio(xio);
    }
    return ret;
}

