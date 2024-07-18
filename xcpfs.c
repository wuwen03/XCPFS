#include"xcpfs.h"

#define DEVELOP
#ifdef DEVELOP
#define CONFIG_BLK_DEV_ZONED
#endif

#include<linux/init.h>
#include<linux/module.h>
#include<linux/mount.h>
#include<linux/uaccess.h>
#include<linux/blkdev.h>
#include<linux/blkzoned.h>

static int xcpfs_report_zones_cb(struct blk_zone *zone, unsigned int idx, void *data) {
    struct super_block *sb = (struct super_block *)data;
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi = &zm->zone_info[idx];

    zm->zone_size = zone->len;
    // zm->zone_capacity = *(sector_t *)((char *)zone->reserved+4);
    zm->zone_capacity = zone->capacity;

    zi->cond = zone->cond;
    zi->wp = zone->wp;
    zi->zone_id = idx;
    zi->start = zone->start;

    if(zone->cond == BLK_ZONE_COND_IMP_OPEN || zone->cond == BLK_ZONE_COND_EXP_OPEN) {
        zm->zone_opened[zm->zone_opened_count++] = zi;
        zm->zone_active[zm->zone_active_count++] = zi;
    } else if(zone->cond == BLK_ZONE_COND_CLOSED) {
        zm->zone_active[zm->zone_active_count++] = zi;
    }
    return 0;
}

//初始化部分的
static int xcpfs_init_zm_info(struct super_block *sb) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct block_device *bdev = sb->s_bdev;
    int ret = 0;

    spin_lock_init(&zm->zm_info_lock);
    init_xpcfs_rwsem(&zm->zm_info_sem);
    zm->nr_zones = bdev_nr_zones(bdev);
    zm->max_open_zones = bdev_max_open_zones(bdev);
    zm->max_active_zones = bdev_max_active_zones(bdev);
    // zm->zone_capacity = get_capacity(bdev->bd_disk);
    zm->zone_info = kmalloc(sizeof(struct xcpfs_zone_info) * zm->nr_zones, GFP_KERNEL);
    zm->zone_opened = kmalloc(sizeof(struct xcpfs_zone_info*) * zm->max_open_zones, GFP_KERNEL);
    zm->zone_active = kmalloc(sizeof(struct xcpfs_zone_info*) * zm->max_active_zones, GFP_KERNEL);
    ret = blkdev_report_zones(bdev,0,BLK_ALL_ZONES,xcpfs_report_zones_cb,sb);
    return ret;
}

static int xcpfs_init_nat_info(struct super_block *sb) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_nat_info *nm = sbi->nm;
    // spin_lock_init(&nm->nat_info_rwsem);
    init_xpcfs_rwsem(&nm->nat_info_rwsem)
    nm->cached_nat_count = 0;
    INIT_LIST_HEAD(&nm->nat_list);
    INIT_LIST_HEAD(&nm->free_nat);
    return 0;
}

static void xcpfs_read_super(struct super_block *sb) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zone0,*zone1;
    struct xcpfs_super_block *raw_super;
    struct page *page;
    struct xcpfs_nat_entry_sb *ne;
    int ret = 0;
    int i;
    int temp;

    zone0 = zm->zone_info[0],zone1 = zm->zone_info[1];
    if(zone0->cond == BLK_ZONE_COND_EMPTY) {
        page = xcpfs_grab_page(sb,sector_to_block(zone1->wp));
    } else if(zone1->cond == BLK_ZONE_COND_EMPTY) {
        page = xcpfs_grab_page(sb,sector_to_block(zone0->wp));
    } else if(zone0->wp > zone1->wp - zm->zone_size) {
        page = xcpfs_grab_page(sb,sector_to_block(zone1->wp));
    } else {
        page = xcpfs_grab_page(sb,sector_to_block(zone0->wp));
    }
    if(IS_ERR(page)) {
        goto free_page;
    }
    
    raw_super = (struct xcpfs_super_block *)page_address(page);
    if(raw_super->magic != XCPFS_MAGIC) {
        ret = -1;
        goto free_page;
    }

    sbi->root_ino = raw_super->root_ino;
    sbi->meta_ino = raw_super->meta_ino;
    sbi->node_ino = raw_super->node_ino;
    sbi->nat_page_count = raw_super->nat_page_count;
    sbi->zit_page_count = raw_super->zit_page_count;
    sbi->ssa_page_count = raw_super->ssa_page_count;
    sbi->total_meta_paga_count = sbi->nat_page_count + sbi->zit_page_count + sbi->ssa_page_count;

    for(i = 0; i < 6; i++) {
        ne = &raw_super->meta_nat[i];
        insert_nat(sb,ne->nid,ne->ne.ino,ne->ne.block_addr,true,true);
    }
free_page:
    xcpfs_free_page(page);
    return ret;
}

static void xcpfs_get_zit_info(struct super_block *sb) {
//TODO
    
}

static int xcpfs_fill_super(struct super_block* sb, void* data, int silent) {
    struct xcpfs_sb_info *sbi;

    sbi = kzalloc(sizeof(struct xcpfs_sb_info),GFP_KERNEL);
    if(!sbi) {
        return -ENOMEM;
    }
    sb->s_fs_info = sbi;
    sbi->sb = sb;

    init_xpcfs_rwsem(&sbi->cp_sem);

    sbi->zm = kmalloc(sizeof(struct xcpfs_zm_info),GFP_KERNEL);
    xcpfs_init_zm_info(sb);

    sbi->nm = kmalloc(sizeof(struct xcpfs_nat_info),GFP_KERNEL);
    xcpfs_init_nat_info(sb);

    xcpfs_read_super(sb);

    sbi->meta_ino = 1;
    sbi->meta_inode = xcpfs_iget(sb,sbi->meta_ino);

    sbi->node_ino = 2;
    sbi->node_inode = xcpfs_iget(sb,sbi->node_ino);

    xcpfs_get_zit_info(sb);
    //TODO
}

static struct dentry* xcpfs_mount(struct file_system_type* fs_type, int flags,
    const char* dev_name, void* data) {
    DEBUG_AT;
    printk(KERN_INFO"in xcpfs_mount");
    printk(KERN_INFO"%s", dev_name);
    return mount_bdev(fs_type, flags, dev_name, data, xcpfs_fill_super);
}

static void kill_xcpfs_super(struct super_block* sb) {
    DEBUG_AT;
    struct inode *inode;
    list_for_each_entry(inode,&sb->s_inodes,i_sb_list) {
        XCPFS_INFO("ino:%d i_nlink:%d i_count:%d",
                        inode->i_ino,inode->i_nlink,inode->i_count);
    }
    kill_block_super(sb);
}
struct file_system_type xcpfs_fs_type = {
    .owner = THIS_MODULE,
    .name = "xcpfs",
    .mount = xcpfs_mount,
    .kill_sb = kill_xcpfs_super,
    .fs_flags = FS_REQUIRES_DEV
};
MODULE_ALIAS_FS("xcpfs");

static int __init init_xcpfs_fs(void) {
    return register_filesystem(&xcpfs_fs_type);
}

static void __exit exit_xcpfs_fs(void) {
    unregister_filesystem(&xcpfs_fs_type);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("xcp");
MODULE_DESCRIPTION("xcpfs");

module_init(init_xcpfs_fs);
module_exit(exit_xcpfs_fs);