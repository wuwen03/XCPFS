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
    for(i = 0; i < len; i++) {
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

//copy from blkdev_zone_mgmt but delete cond_sched()
static int xcpfs_blkdev_zone_mgmt(struct block_device *bdev, enum req_op op,
		     sector_t sector, sector_t nr_sectors, gfp_t gfp_mask)
{
	struct request_queue *q = bdev_get_queue(bdev);
	sector_t zone_sectors = bdev_zone_sectors(bdev);
	sector_t capacity = bdev_nr_sectors(bdev);
	sector_t end_sector = sector + nr_sectors;
	struct bio *bio = NULL;
	int ret = 0;

	if (!bdev_is_zoned(bdev))
		return -EOPNOTSUPP;

	if (bdev_read_only(bdev))
		return -EPERM;

	if (!op_is_zone_mgmt(op))
		return -EOPNOTSUPP;

	if (end_sector <= sector || end_sector > capacity)
		/* Out of range */
		return -EINVAL;

	/* Check alignment (handle eventual smaller last zone) */
	if (!bdev_is_zone_start(bdev, sector))
		return -EINVAL;

	if (!bdev_is_zone_start(bdev, nr_sectors) && end_sector != capacity)
		return -EINVAL;

	/*
	 * In the case of a zone reset operation over all zones,
	 * REQ_OP_ZONE_RESET_ALL can be used with devices supporting this
	 * command. For other devices, we emulate this command behavior by
	 * identifying the zones needing a reset.
	 */
	// if (op == REQ_OP_ZONE_RESET && sector == 0 && nr_sectors == capacity) {
	// 	if (!blk_queue_zone_resetall(q))
	// 		return blkdev_zone_reset_all_emulated(bdev, gfp_mask);
	// 	return blkdev_zone_reset_all(bdev, gfp_mask);
	// }

	while (sector < end_sector) {
		bio = blk_next_bio(bio, bdev, 0, op | REQ_SYNC, gfp_mask);
		bio->bi_iter.bi_sector = sector;
		sector += zone_sectors;

		/* This may take a while, so be nice to others */
		// cond_resched();
	}

	ret = submit_bio_wait(bio);
	bio_put(bio);

	return ret;
}

static int xcpfs_zone_open(struct super_block *sb,int zone_id) {
    DEBUG_AT;
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
    // preempt_disable();
    // spin_unlock(&zm->zm_info_lock);
    ret = xcpfs_blkdev_zone_mgmt(sb->s_bdev,REQ_OP_ZONE_OPEN,zi->start,zm->zone_size,GFP_NOFS);
    // preempt_enable();
    // spin_lock(&zm->zm_info_lock);
    if (ret) {
        XCPFS_INFO("open zone error:%d",zone_id);
        goto out;
    }
    insert_zone_opened(sb,zi);
    if(zi->cond != BLK_ZONE_COND_CLOSED) {
        insert_zone_active(sb,zi);
    }
    zi->cond = BLK_ZONE_COND_EXP_OPEN;
    zi->dirty = true;
out:
    return ret;
}

static int xcpfs_zone_close(struct super_block *sb,int zone_id) {
    DEBUG_AT;
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi;
    int i;
    int ret;
    ret = -EZONE;
    zi = &zm->zone_info[zone_id];
    if(zi->cond != BLK_ZONE_COND_EXP_OPEN) {
        goto out;
    }
    
    ret = xcpfs_blkdev_zone_mgmt(sb->s_bdev,REQ_OP_ZONE_CLOSE,zi->start,zm->zone_size,GFP_NOFS);
    if(ret) {
        goto out;
    }
    remove_zone_opened(sb,zi);
    zi->cond = BLK_ZONE_COND_CLOSED;
    zi->dirty = true;
out:
    return ret;
}

static int xcpfs_zone_finish(struct super_block *sb, int zone_id) {
    DEBUG_AT;
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi;
    int i;
    int ret;
    ret = -EZONE;
    zi = &zm->zone_info[zone_id];
    if(zi->cond == BLK_ZONE_COND_EMPTY) {
        goto out;
    }
    ret = xcpfs_blkdev_zone_mgmt(sb->s_bdev,REQ_OP_ZONE_FINISH,zi->start,zm->zone_size,GFP_NOFS);
    if(ret) {
        goto out;
    }
    remove_zone_opened(sb,zi);
    remove_zone_active(sb,zi);
    zi->cond = BLK_ZONE_COND_FULL;
    zi->wp = zm->zone_capacity;
    zi->dirty = true;
out:
    return ret;
}

static int xcpfs_zone_reset(struct super_block *sb, int zone_id) {
    DEBUG_AT;
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi;
    int i;
    int ret;
    ret = -EZONE;
    zi = &zm->zone_info[zone_id];
    ret = xcpfs_blkdev_zone_mgmt(sb->s_bdev,REQ_OP_ZONE_RESET,zi->start,zm->zone_size,GFP_NOFS);

    if(ret) {
        goto out;
    }
    remove_zone_opened(sb,zi);
    remove_zone_active(sb,zi);
    zi->cond = BLK_ZONE_COND_EMPTY;
    zi->wp = 0;
    zi->dirty = true;
out:
    return ret;
}

int xcpfs_zone_mgmt(struct super_block *sb,int zone_id,enum req_op op) {
    DEBUG_AT;
    int ret;
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    spin_lock(&sbi->zm->zm_info_lock);
    // xcpfs_down_write(&sbi->zm->zm_info_sem);
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
        default:
            ret = -EIO;
    }
    // xcpfs_up_write(&sbi->zm->zm_info_sem);
    spin_unlock(&sbi->zm->zm_info_lock);
    return ret;
}

int validate_blkaddr(struct super_block *sb, block_t blkaddr) {
    DEBUG_AT;
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi;
    int zone_id;
    int idx;
    // return 0; //TODO
    zone_id = blkaddr / (zm->zone_size >> PAGE_SECTORS_SHIFT);
    idx = blkaddr % (zm->zone_size >> PAGE_SECTORS_SHIFT);

    zi = &zm->zone_info[zone_id];

    spin_lock(&zm->zm_info_lock);
    set_bit(idx,zi->valid_map);
    zi->vblocks ++;
    zi->dirty = true;
    zi->wp = max(zi->wp,(blkaddr + 1) << PAGE_SECTORS_SHIFT);
    // zi->wp = umax(zi->wp,blkaddr << PAGE_SECTORS_SHIFT);
    XCPFS_INFO("wp:0x%x capacity:0x%x",zi->wp, zm->zone_capacity)
    if(zi->wp == zm->zone_capacity + zi->start) {
        zi->cond = BLK_ZONE_COND_FULL;
        remove_zone_active(sb,zi);
        remove_zone_opened(sb,zi);
    }
    spin_unlock(&zm->zm_info_lock);

    return 0;
}

int invalidate_blkaddr(struct super_block *sb, block_t blkaddr) {
    DEBUG_AT;
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi;
    int zone_id;
    int idx;
    // return 0; //TODO
    zone_id = blkaddr / (zm->zone_size >> PAGE_SECTORS_SHIFT);
    idx = blkaddr % (zm->zone_size >> PAGE_SECTORS_SHIFT);
    
    spin_lock(&zm->zm_info_lock);
    zi = &zm->zone_info[zone_id];
    clear_bit(idx,zi->valid_map);
    zi->vblocks --;
    zi->dirty = true;
    spin_unlock(&zm->zm_info_lock);

    return 0;
}

static int get_empty_zone(struct super_block *sb) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi;
    int i;
    for(i = 2; i < zm->nr_zones; i++) {
        zi = &zm->zone_info[i];
        if(zi->cond == BLK_ZONE_COND_EMPTY) {
            return zi->zone_id;
        }
    }
    //TODO GC
    return 0;
}

static bool zone_is_full(struct super_block *sb, int zone_id) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi;
    
    zi = &zm->zone_info[zone_id];
    return zi->cond == BLK_ZONE_COND_FULL;
}

int blk2zone(struct super_block *sb, block_t iblock) {
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_zm_info *zm = sbi->zm;
    return iblock / (zm->zone_size >> PAGE_SECTORS_SHIFT);
}

static int get_zone(struct super_block *sb, enum page_type type) {
    struct xcpfs_sb_info *sbi = sb->s_fs_info;
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *zi, *tar = NULL;
    int i, zone_id;
    sector_t inflight = LONG_MAX;
    int ret = -EZONE;
    if(zm->nr_type[type] < zm->limit_type[type]) {
        goto open;
    }
retry:
    tar = NULL;
    for (i = 0; i < zm->max_open_zones; i ++) {
        zi = zm->zone_opened[i];
        if(!zi) continue;
        if(zi->zone_type == type && zi->wp + zi->write_inflight - zi->start < inflight) {
            inflight = zi->wp + zi->write_inflight;
            tar = zi;
        }
        // if(zi->zone_type == type) {
        //     return zi->zone_id;
        // }
    }
    if(tar) {
        return tar->zone_id;
    }
open:
    if(zm->zone_opened_count < zm->max_open_zones) {
        zone_id = get_empty_zone(sb);
        xcpfs_zone_open(sb,zone_id);
        zi = &zm->zone_info[zone_id];
        zi->zone_type = type;
        zm->nr_type[type] ++;
        if(xcpfs_rwsem_is_locked(&sbi->cp_sem)) {
            sbi->cpc->restart = true;
        }
    }
    goto retry;
}
//fill xio->new_blkaddr according to the op and type in the xio
void alloc_zone(struct xcpfs_io_info *xio) {
    DEBUG_AT;
    struct xcpfs_sb_info *sbi = xio->sbi;
    struct xcpfs_zm_info *zm = sbi->zm;
    enum req_op op = xio->op;
    int zone_id;
    unsigned long flags;

    if(op == REQ_OP_READ) {
        XCPFS_INFO("read op old addr:0x%x",xio->old_blkaddr);
        xio->new_blkaddr = xio->old_blkaddr;
    } else {
        zone_id = blk2zone(sbi->sb,xio->old_blkaddr);
        XCPFS_INFO("write op zone id:0x%x",zone_id);

        spin_lock(&zm->zm_info_lock);
        // spin_lock_irqsave(&zm->zm_info_lock,flags);
        if(zone_is_full(sbi->sb,zone_id) || zone_id == 0) {
            zone_id = get_zone(sbi->sb,xio->type);
        }
        xio->new_blkaddr = zm->zone_info[zone_id].start >> PAGE_SECTORS_SHIFT;
        spin_unlock(&zm->zm_info_lock);
        
        invalidate_blkaddr(sbi->sb,xio->old_blkaddr);
        // spin_unlock_irqrestore(&zm->zm_info_lock,flags);
        XCPFS_INFO("write op zoneid:%d zslba:0x%x",zone_id,xio->new_blkaddr);
    }
    
}

int start_flight(struct super_block *sb, int zone_id) {
    DEBUG_AT;
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_zm_info *zm = sbi->zm;
    // spin_lock(&zm->zm_info_lock);
    // zm->zone_info[zone_id].write_inflight += PAGE_SECTORS;
    // spin_unlock(&zm->zm_info_lock);
    return 0;
}

int end_flight(struct super_block *sb, int zone_id) {
    DEBUG_AT;
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_zm_info *zm = sbi->zm;
    // spin_lock(&zm->zm_info_lock);
    // zm->zone_info[zone_id].write_inflight -= PAGE_SECTORS;
    // spin_unlock(&zm->zm_info_lock);
    return 0;
}

int flush_zit(struct super_block *sb) {
    DEBUG_AT;
    struct xcpfs_sb_info *sbi = XCPFS_SB(sb);
    struct xcpfs_zm_info *zm = sbi->zm;
    struct xcpfs_zone_info *entries = zm->zone_info, *zi;
    struct xcpfs_zit_block *raw_entries;
    struct xcpfs_zit_entry *ze;
    struct page *page;
    int i;
    for(i = 0; i < zm->nr_zones; i++) {
        zi = &entries[i];
        if(zi->dirty == false) {
            continue;
        }
        XCPFS_INFO("zoneid:%d start:%x wp:%x cond:%d type:%d",zi->zone_id,zi->start,zi->wp,zi->cond,zi->zone_type);
        page = xcpfs_prepare_page(sbi->meta_inode,i / ZIT_ENTRY_PER_BLOCK + ZIT_START,true,true);
        raw_entries = (struct xcpfs_zit_block *)page_address(page);
        ze = &raw_entries->entries[i % ZIT_ENTRY_PER_BLOCK];
        ze->zone_type = zi->zone_type;
        ze->vblocks = zi->vblocks;
        memcpy(ze->valid_map,zi->valid_map,ZONE_VALID_MAP_SIZE);
        xcpfs_commit_write(page,page_offset(page),PAGE_SIZE);
        zi->dirty = false;
    }
    return 0;
}