#include "xcpfs.h"

static int __insert_zone(struct xcpfs_zone_info **list,int len,struct xcpfs_zone_info *tar) {
    int i;
    if(!tar || !list) {
        return -1;
    }
    for(i = 0; i < len; i++) {
        if(list[i] == NULL) {
            list[i] = tar;
            return 0;
        }
    }
    return -1;
}

static int __remove_zone(struct xcpfs_zone_info **list,int len,struct xcpfs_zone_info *tar) {
    int i;
    if(!tar || !list) {
        return -1;
    }
    for(i = 0; ;i < len; i++) {
        if(list[i] == tar) {
            list[i] = NULL;
            return 0;
        }
    }
    return -1;
}

static int insert_zone_opened(struct super_block *sb,struct xcpfs_zone_info *tar) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    int ret;
    ret = __insert_zone(sbi->zm->zone_opened,sbi->zm->max_open_zones,tar);
    if(ret) {
        return ret;
    }
    sbi->zm->zone_opened_count++;
    return ret;
}

static int remove_zone_opened(struct super_block *sb,struct xcpfs_zone_info *tar) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    int ret;
    ret = __remove_zone(sbi->zm->zone_opened,sbi->zm->max_open_zones,tar);
    if(ret) {
        return ret;
    }
    sbi->zm->zone_opened_count--;
    return ret;
}

static int insert_zone_active(struct super_block *sb,struct xcpfs_zone_info *tar) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    int ret;
    ret = __insert_zone(sbi->zm->zone_active,sbi->zm->max_active_zones,tar);
    if(ret) {
        return ret;
    }
    sbi->zm->zone_active_count++;
    return ret;
}

static int remove_zone_active(struct super_block *sb,struct xcpfs_zone_info *tar) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    int ret;
    ret = __remove_zone(sbi->zm->zone_active,sbi->zm->max_active_zones,tar);
    if(ret) {
        return ret;
    }
    sbi->zm->zone_active_count--;
    return ret;
}

static int xcpfs_zone_open(struct super_block *sb,int zone_id) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi;
    int i;
    int ret;
    ret = 0;
    zi = &zm->zone_info[zone_id];
    if(zi->cond == BLK_ZONE_COND_EXP_OPEN) {
        goto out;
    }
    if(zm->zone_opened_count == zm->max_open_zones) {
        ret = -EMAXOPEN;
        goto out;
    }
    if(zm->zone_active_count == zm->max_active_zones) {
        ret = -EMAXACTIVE;
        goto out;
    }
    ret = blkdev_zone_mgmt(sb->s_bdev,REQ_OP_ZONE_OPEN,zi->start,zm->zone_size,GFP_NOFS);
    if (ret) {
        XCPFS_INFO("open zone error:%d",zone_id);
        goto out;
    }
    insert_zone_opened(sb,zi);
    if(zi->cond != BLK_ZONE_COND_CLOSED) {
        insert_zone_active(sb,zi);
    }
    zi->cond = BLK_ZONE_COND_EXP_OPEN;
out:
    return ret;
}

static int xcpfs_zone_close(struct super_block *sb,int zone_id) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi;
    int i;
    int ret;
    ret = -EZONE;
    zi = zm->zone_info[zone_id];
    if(zi->cond != BLK_ZONE_COND_EXP_OPEN) {
        goto out;
    }
    ret = blkdev_zone_mgmt(sb->s_bdev,REQ_OP_ZONE_CLOSE,zi->start,zm->zone_size,GFP_NOFS);
    if(ret) {
        goto out;
    }
    remove_zone_opened(sb,zi);
    zi->cond = BLK_ZONE_COND_CLOSED;
out:
    return ret;
}

static int xcpfs_zone_finish(struct super_block *sb, int zone_id) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi;
    int i;
    int ret;
    ret = -EZONE;
    zi = zm->zone_info[zone_id];
    if(zi->cond == BLK_ZONE_COND_EMPTY) {
        goto out;
    }
    ret = blkdev_zone_mgmt(sb->s_bdev,REQ_OP_ZONE_FINISH,zi->start,zm->zone_size,GFP_NOFS);
    if(ret) {
        goto out;
    }
    remove_zone_opened(sb,zi);
    remove_zone_active(sb,zi);
    zi->cond = BLK_ZONE_COND_FULL;
    zi->wp = zm->zone_capacity;
out:
    return ret;
}

static int xcpfs_zone_reset(struct super_block *sb, int zone_id) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi;
    int i;
    int ret;
    ret = -EZONE;
    zi = zm->zone_info[zone_id];
    ret = blkdev_zone_mgmt(sb->s_bdev,REQ_OP_ZONE_RESET,zi->start,zm->zone_size,GFP_NOFS);
    if(ret) {
        goto out;
    }
    remove_zone_opened(sb,zi);
    remove_zone_active(sb,zi);
    zi->cond = BLK_ZONE_COND_EMPTY;
    zi->wp = 0;
out:
    return ret;
}

int xcpfs_zone_mgmt(struct super_block *sb,int zone_id,enum req_op op) {

    int ret;
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    spin_lock(&sbi->zm->zm_info_lock);
    xcpfs_down_write(&sbi->zm->zm_info_sem);
    switch( op ){
        case REQ_OP_ZONE_OPEN:
            ret = xcpfs_zone_open(sb,zone_id);
            break;
        case REQ_OP_ZONE_CLOSE:
            ret = xcpfs_zone_close(sb,zone_id);
            break;
        case REQ_OP_ZONE_FINISH:
            ret = xcpfs_zone_finish(sb,zone_id);
            break;
        case REQ_OP_ZONE_RESET:
            ret = xcpfs_zone_reset(sb,zone_id);
            break;
    }
    spin_unlock(&sbi->zm->zm_info_lock);
    xcpfs_up_write(&sbi->zm->zm_info_sem);
    return ret;
}

static int validate_blkaddr(struct super_block *sb, block_t blkaddr) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi;
    int zone_id;
    int idx;

    zone_id = blkaddr / (zm->zone_size >> PAGE_SECTORS_SHIFT);
    idx = blkaddr % (zm->zone_size >> PAGE_SECTORS_SHIFT);
    
    zi = zm->zone_info[zone_id];
    set_bit(idx,zi->valid_map);
    zi->vblocks ++;
    zi->dirty = true;
    return 0;
}

static int invalidate_blkaddr(struct super_block *sb, block_t blkaddr) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi;
    int zone_id;
    int idx;

    zone_id = blkaddr / (zm->zone_size >> PAGE_SECTORS_SHIFT);
    idx = blkaddr % (zm->zone_size >> PAGE_SECTORS_SHIFT);
    
    zi = zm->zone_info[zone_id];
    clear_bit(idx,zi->valid_map);
    zi->vblocks --;
    zi->dirty = true;
    return 0;
}

static int get_empty_zone(struct super_block *sb) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi;
    int i;
    for(i = 2; i < zm->nr_zones; i++) {
        zi = zm->zone_info[i];
        if(zi->cond = BLK_ZONE_COND_EMPTY) {
            return zi->zone_id;
        }
    }
    //TODO GC
}

static bool zone_is_full(struct super_block *sb, int zone_id) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi;
    
    zi = zm->zone_info[zone_id];
    return zi->cond == BLK_ZONE_COND_FULL;
}

static int blk2zone(struct super_block *sb, block_t iblock) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_zm_info *zm = sbi->zm;
    return iblock / (zm->zone_size >> PAGE_SECTORS_SHIFT);
}

static int get_zone(struct super_block *sb, enum page_type type) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi;
    int i;
    int zone_id;
    int ret = -EZONE;
retry:
    for (i = 0; i < zm->max_open_zones; i++) {
        zi = zm->zone_opened[i];
        if(!zi) continue;
        if(zi->zone_type == type) {
            return zi->zone_id;
        }
    }

    if(zm->zone_opened_count < zm->max_open_zones) {
        zone_id = get_empty_zone(sb);
        xcpfs_zone_open(sb,zone_id);
        zi = zm->zone_info[zone_id];
        zi->zone_type = type;
    }
    goto retry;
}
//fill xio->new_blkaddr according to the op and type in the xio
void alloc_zone(struct xcpfs_io_info *xio) {
    struct xcpfs_sb_info *sbi = xio->sbi;
    struct xcpfs_zm_info *zm = sbi->zm;
    enum req_op op = xio->op;
    int zone_id;
    if(op == REQ_OP_READ) {
        xio->new_blkaddr = xio->old_blkaddr;
    } else {
        zone_id = blk2zone(sbi->sb,xio->old_blkaddr);
        spin_lock(&zm->zm_info_lock);
        if(zone_is_full(sbi->sb,zone_id) || zone_id == 0) {
            zone_id = get_zone(sbi->sb,xio->type);
        }
        // xio->new_blkaddr = zone_id * (zm->zone_size >> PAGE_SECTORS_SHIFT);
        xio->new_blkaddr = zm->zone_info[zone_id].start >> PAGE_SECTORS_SHIFT;
        validate_blkaddr(sbi->sb,xio->new_blkaddr);
        invalidate_blkaddr(sbi->sb,xio->old_blkaddr);
        spin_unlock(&zm->zm_info_lock);
    }
}