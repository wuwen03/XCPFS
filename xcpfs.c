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

int sector_to_block(sector_t sector) {
	return sector >> 3;
}

struct xcpfs_sb_info* XCPFS_SB(struct super_block *sb) {
	return sb->s_fs_info;
}

static int xcpfs_report_zones_cb(struct blk_zone *zone, unsigned int idx, void *data) {
    struct super_block *sb = (struct super_block *)data;
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi = &zm->zone_info[idx];

    zm->zone_size = zone->len;
    zm->zone_capacity = zone->capacity;

    zi->cond = zone->cond;
    zi->wp = zone->wp;
    zi->zone_id = idx;
    zi->start = zone->start;
    XCPFS_INFO("idx:0x%x start:0x%x wp:0x%x cond:0x%x",idx,zi->start,zi->wp,zi->cond);

    if(zone->cond == BLK_ZONE_COND_IMP_OPEN || zone->cond == BLK_ZONE_COND_EXP_OPEN) {
        zm->zone_opened[zm->zone_opened_count++] = zi;
        zm->zone_active[zm->zone_active_count++] = zi;
    } else if(zone->cond == BLK_ZONE_COND_CLOSED) {
        zm->zone_active[zm->zone_active_count++] = zi;
    }
    return 0;
}

//初始化部分的zit
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
    XCPFS_INFO("nr_zones:0x%x open:%d active:%d",zm->nr_zones,zm->max_open_zones,zm->max_active_zones);
    ret = blkdev_report_zones(bdev,0,BLK_ALL_ZONES,xcpfs_report_zones_cb,sb);
    XCPFS_INFO("size:0x%x cap:0x%x",zm->zone_size,zm->zone_capacity);
    XCPFS_INFO("ret:%d",ret);

    zm->limit_type[ZONE_TYPE_META_DATA] = 1;
    zm->limit_type[ZONE_TYPE_META_DATA] = 1;
    zm->limit_type[ZONE_TYPE_NODE] = 2;
    zm->limit_type[ZONE_TYPE_DATA] = zm->max_open_zones - 1 - 1 - 2 - 1 - 1; //1 superblock 1 reserved

    return 0;
}
//初始化部分nat
static int xcpfs_init_nat_info(struct super_block *sb) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_nat_info *nm = sbi->nm;
    // spin_lock_init(&nm->nat_info_rwsem);
    init_xpcfs_rwsem(&nm->nat_info_rwsem)
    nm->cached_nat_count = 0;
    INIT_LIST_HEAD(&nm->nat_list);
    INIT_LIST_HEAD(&nm->free_nat);
    INIT_LIST_HEAD(&nm->cp_nat);
    return 0;
}
//读取super block 并且填充meta nat
static int xcpfs_read_super(struct super_block *sb) {
    DEBUG_AT;
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zone0,*zone1;
    struct xcpfs_super_block *raw_super;
    struct page *page;
    struct xcpfs_nat_entry_sb *ne;
    int ret = 0,err;
    int i;
    int temp;

    zone0 = &zm->zone_info[0],zone1 = &zm->zone_info[1];
    if(zone0->cond == BLK_ZONE_COND_EMPTY) {
        page = xcpfs_grab_page(sb,sector_to_block(zone1->wp - 1));
    } else if(zone1->cond == BLK_ZONE_COND_EMPTY) {
        page = xcpfs_grab_page(sb,sector_to_block(zone0->wp - 1));
    } else if(zone0->wp > zone1->wp - zm->zone_size) {
        page = xcpfs_grab_page(sb,sector_to_block(zone1->wp - 1));
    } else {
        page = xcpfs_grab_page(sb,sector_to_block(zone0->wp - 1));
    }
    if(IS_ERR_OR_NULL(page)) {
        goto free_page;
    }
    
    raw_super = (struct xcpfs_super_block *)page_address(page);
    XCPFS_INFO("magic:%X  expected:%X",raw_super->magic,XCPFS_MAGIC);
    // XCPFS_INFO("test:%s",(char *)raw_super);
    if(raw_super->magic != XCPFS_MAGIC) {
        ret = -EIO;
        goto free_page;
    }

    sbi->root_ino = raw_super->root_ino;
    sbi->meta_ino = raw_super->meta_ino;
    sbi->node_ino = raw_super->node_ino;
    XCPFS_INFO("ino:root:%d meta:%d node:%d",sbi->root_ino,sbi->meta_ino,sbi->node_ino);
    sbi->nat_page_count = raw_super->nat_page_count;
    sbi->zit_page_count = raw_super->zit_page_count;
    sbi->ssa_page_count = raw_super->ssa_page_count;
    sbi->total_meta_paga_count = sbi->nat_page_count + sbi->zit_page_count + sbi->ssa_page_count;

    for(i = 0; i < raw_super->meta_nat_cnt; i++) {
        ne = &raw_super->meta_nat[i];
        if(ne->nid == 0) {
            continue;
        }
        err = insert_nat(sb,ne->nid,ne->ne.ino,ne->ne.block_addr,true,true);
        if(err = -EEXIST) {
            update_nat(sb,ne->nid,ne->ne.block_addr,true);
        }
        XCPFS_INFO("meta nat: nid:%d,addr:0x%x\n",ne->nid,ne->ne.block_addr);
    }
free_page:
    xcpfs_free_page(page);
    return ret;
}

//TODO
static void xcpfs_get_zit_info(struct super_block *sb) {
    DEBUG_AT;
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zit_block *raw_zit;
    struct xcpfs_zit_entry *ze;
    struct xcpfs_zone_info *zi;
    struct page *page;
    int iblock, i, j;
    for(i = 0; i< zm->nr_zones; i++) {
        zi = &zm->zone_info[i];
        zi->valid_map = kmalloc(sizeof(uint8_t) * ZONE_VALID_MAP_SIZE * 4,GFP_KERNEL);
        zi->write_inflight = 0;
    }
    for(i = 0; i < DIV_ROUND_UP(zm->nr_zones,ZIT_ENTRY_PER_BLOCK); i ++) {
        iblock = i + ZIT_START;
        page = xcpfs_prepare_page(sbi->meta_inode,iblock,false,false);
        if(IS_ERR_OR_NULL(page)) {
            continue;
        }
        raw_zit =(struct xcpfs_zit_block *)page_address(page);
        ze = raw_zit->entries;
        for(j = 0; j < ZIT_ENTRY_PER_BLOCK; j ++) {
            if(i * ZIT_ENTRY_PER_BLOCK + j == zm->nr_zones) {
                break;
            }
            zi = &zm->zone_info[i * ZIT_ENTRY_PER_BLOCK + j];
            if(zi->cond == BLK_ZONE_COND_EMPTY) {
                continue;
            }
            zi->zone_type = ze[j].zone_type;
            zi->vblocks = ze[j].vblocks;
            zi->dirty = true;
            memcpy(zi->valid_map,ze->valid_map,ZONE_VALID_MAP_SIZE);
            zm->nr_type[zi->zone_type] ++;
            XCPFS_INFO("zoneid:%d start:%x wp:%x cond:%d type:%d",zi->zone_id,zi->start,zi->wp,zi->cond,zi->zone_type);
        }
        unlock_page(page);
        put_page(page);
    }
}

static int xcpfs_fill_super(struct super_block* sb, void* data, int silent) {
    struct xcpfs_sb_info *sbi;
    struct inode *root_inode;
    int err = 0;

    sbi = kzalloc(sizeof(struct xcpfs_sb_info),GFP_KERNEL);
    if(!sbi) {
        return -ENOMEM;
    }
    sb->s_fs_info = sbi;
    sbi->sb = sb;

    init_xpcfs_rwsem(&(sbi->cp_sem));

    sbi->zm = kmalloc(sizeof(struct xcpfs_zm_info),GFP_KERNEL);
    err = xcpfs_init_zm_info(sb);
    sbi->zm->zone_info[3].zone_type = ZONE_TYPE_META_NODE;
    sbi->zm->zone_info[3].dirty = true;
    // sbi->zm->zone_info[3].vblocks = kmalloc(sizeof(uint8_t) * ZONE_VALID_MAP_SIZE,GFP_KERNEL);
    sbi->zm->zone_info[5].zone_type = ZONE_TYPE_NODE;
    sbi->zm->zone_info[5].dirty = true;
    // sbi->zm->zone_info[5].vblocks = kmalloc(sizeof(uint8_t) * ZONE_VALID_MAP_SIZE,GFP_KERNEL);
    sbi->zm->zone_info[6].zone_type = ZONE_TYPE_DATA;
    sbi->zm->zone_info[6].dirty = true;
    // sbi->zm->zone_info[6].vblocks = kmalloc(sizeof(uint8_t) * ZONE_VALID_MAP_SIZE,GFP_KERNEL);

    if(err) {
        XCPFS_INFO("err after init_zm_info");
        return err;
    }

    sbi->nm = kmalloc(sizeof(struct xcpfs_nat_info),GFP_KERNEL);
    xcpfs_init_nat_info(sb);

    xcpfs_read_super(sb);

    sbi->node_inode = xcpfs_iget(sb,sbi->node_ino);
    if(!sbi->node_inode) {
        XCPFS_INFO("node inode error");
        return -EIO;
    }
    sbi->meta_inode = xcpfs_iget(sb,sbi->meta_ino);
    if(!sbi->meta_inode) {
        XCPFS_INFO("meta inode error");
        return -EIO;
    }
    root_inode = xcpfs_iget(sb,sbi->root_ino);
    if(!root_inode) {
        XCPFS_INFO("root inode error");
        return -EIO;
    }
    
    xcpfs_get_zit_info(sb);


    sb->s_op = &xcpfs_sops;
    sb->s_root = d_make_root(root_inode);

    sbi->cp_phase = 0;
    //TODO
    return 0;
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
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    if(!sb) {
        return;
    }
    list_for_each_entry(inode,&sb->s_inodes,i_sb_list) {
        XCPFS_INFO("ino:%d i_nlink:%d i_count:%d",
                        inode->i_ino,inode->i_nlink,inode->i_count);
    }
    do_checkpoint(sb);
    XCPFS_INFO("----------after check point---------");
    list_for_each_entry(inode,&sb->s_inodes,i_sb_list) {
        if(!inode) continue;
        XCPFS_INFO("ino:%d i_nlink:%d i_count:%d",
                        inode->i_ino,inode->i_nlink,inode->i_count);
    }
    iput(sbi->meta_inode);
    iput(sbi->node_inode);
    kill_block_super(sb);
    XCPFS_INFO("after kill_block_super");   
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