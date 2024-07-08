#ifndef XCPFS_H
#define XCPFS_H

#ifndef __KERNEL__
#define __KERNEL__
#endif

#define DEVELOP
#ifdef DEVELOP
#define CONFIG_BLK_DEV_ZONED
#endif

#include<linux/fs.h>
#include<linux/mutex.h>
#include<linux/semaphore.h>
#include<linux/bitmap.h>
#include<linux/list.h>
#include<linux/math.h>
#include<linux/hash.h>
#include<linux/bitmap.h>
#include<linux/printk.h>
#include<linux/buffer_head.h>
#include<linux/writeback.h>
#include<linux/stat.h>
#include<linux/mm.h>
#include<linux/statfs.h>
#include<linux/blkdev.h>
#include<linux/blkzoned.h>
// #include<asm-generic>

#include"xcpfs_rwsem.h"


#define DEBUG_AT \
    do {\
        printk(KERN_WARNING"%s,%d:",__FUNCTION__,__LINE__);\
    }while(0);\
    
#define XCPFS_INFO(str,args...) \
do{\
    printk(KERN_INFO "%s,%d: " str,__FUNCTION__,__LINE__,##args);\
}while(0);\

#define BLOCK_SIZE 4096
#define BLOCK_SIZE_BITS 12
#define BLOCKS_PER_ZONE 
#define PAGE_SIZE 4096
#define XCPFS_MAGIC 0x52064
typedef int block_t; /*block地址*/
typedef int nid_t;

inline int sector_to_block(sector_t sector) {
	return sector >> 3;
}



struct xcpfs_nat_entry;
//on disk super block
struct xcpfs_super_block {
    __le32 magic;
    __le32 root_ino;
	__le32 meta_ino;
	__le32 node_ino;
	__le32 nat_page_count;
	__le32 zit_page_count;
	__le32 ssa_page_count;
	struct xcpfs_nat_entry meta_nat[1+2+2+1];//inode(1),direct(2),indirect(2),double_indirect(1)
} __packed;

#define DEF_ADDRS_PER_INODE 900
#define DEF_NIDS_PER_INODE 5
#define DEF_ADDRS_PER_BLOCK         1018	/* Address Pointers in a Direct Block */
#define DEF_NIDS_PER_BLOCK          1018	/* Node IDs in an Indirect Block */
//on disk inode 部分来自于f2fs 在实现的过程中，先不打算实现double_indirect
struct xcpfs_inode {
    __le16 i_mode;			/* file mode */
	// __u8 i_advise;			/* file hints */
	// __u8 i_inline;			/* file inline flags */
	__le32 i_uid;			/* user ID */
	__le32 i_gid;			/* group ID */
	__le32 i_links;			/* links count */
	__le64 i_size;			/* file size in bytes */
	__le64 i_blocks;		/* file size in blocks */
	__le64 i_atime;			/* access time */
	__le64 i_ctime;			/* change time */
	__le64 i_mtime;			/* modification time */
	__le32 i_atime_nsec;		/* access time in nano scale */
	__le32 i_ctime_nsec;		/* change time in nano scale */
	__le32 i_mtime_nsec;		/* modification time in nano scale */
	__le32 i_generation;		/* file version (for NFS) */
	
	__le32 i_addr[DEF_ADDRS_PER_INODE];	/* Pointers to data blocks */
	__le32 i_nid[DEF_NIDS_PER_INODE];	/* direct(2), indirect(2),
						double_indirect(1) node id */
} __packed;

struct direct_node {
	__le32 addr[DEF_ADDRS_PER_BLOCK];
};

struct indirect_node {
	__le32 nid[DEF_NIDS_PER_BLOCK];
};

//这个结构体也是f2fs原来的，不过为了实现简单，只会用到部分
struct node_footer {
	__le32 nid;		/* node id */
	__le32 ino;		/* inode nunmber */
	__le32 flag;		/* include cold/fsync/dentry marks and offset */
	__le64 cp_ver;		/* checkpoint version */
	__le32 next_blkaddr;	/* next node page block address */
} __packed;

struct xcpfs_node {
	union {
		struct xcpfs_inode i;
		struct direct_node dn;
		struct indirect_node in;
	};
	struct node_footer footer;
} __packed;

#define ZONE_VALID_MAP_SIZE 64
#define SIT_ENTRY_PER_BLOCK (PAGE_SIZE / sizeof(struct xcpfs_sit_entry))
#define ZIT_ENTRY_SIZE (sizeof(struct xcpfs_zit_entry))
#define ZONE_TYPE_FREE 0
#define ZONE_TYPE_META_NODE 1
#define ZONE_TYPE_META_DATA 2
#define ZONE_TYPE_NODE 3
#define ZONE_TYPE_DATA 4
//on disk zone information table entry 和f2fs相同，不过可能mtime会用不到，因为只进行简单的gc
struct xcpfs_zit_entry {
	__le16 zone_type:6;
    __le16 vblocks:10;
	// __le16 zone_id;
	// __le16 idx;
    uint8_t valid_map[ZONE_VALID_MAP_SIZE];
	__le64 mtime;
} __packed;

struct xcpfs_zit_block {
	struct xcpfs_zit_entry entries[SIT_ENTRY_PER_BLOCK];
} __packed;

#define NAT_ENTRY_SIZE (sizeof(struct xcpfs_nat_entry))
#define NAT_ENTRY_PER_BLOCK (PAGE_SIZE/NAT_ENTRY_SIZE)
//on disk nat entry
struct xcpfs_nat_entry {
	uint8_t version;
    __le32 ino;
	__le32 block_addr;
} __packed;

struct xcpfs_nat_block {
	struct xcpfs_nat_entry entries[NAT_ENTRY_PER_BLOCK];
} __packed;


#define ENTRIES_IN_SUM 512
/* a summary entry for a 4KB-sized block in a segment */
struct xcpfs_summary {
	__le32 nid;		/* parent node id */
	union {
		uint8_t reserved[3];
		struct {
			uint8_t version;		/* node version number */
			__le16 ofs_in_node;	/* block index in parent node */
		} __packed;
	};
} __packed;

struct xcpfs_summary_block {
	struct xcpfs_summary entries[ENTRIES_IN_SUM];
} __packed;

struct xcpfs_zone_info {
	
	enum blk_zone_cond cond;
	sector_t wp;
	sector_t start;
	
	bool dirty;
	int zone_id;
	uint8_t zone_type;
	int vblocks;
	uint8_t *valid_map;
};

struct xcpfs_zm_info {
	spinlock_t zm_info_lock;
	struct xcpfs_rwsem zm_info_sem;
	sector_t zone_size; 				//size of a zone(in bytes)
	sector_t zone_capacity;			//capacity of a zone(in bytes)
	int nr_zones;				//total zone number

	int max_open_zones;		//most # of zones in open state
	int max_active_zones;	//most # of zones in active state

	struct xcpfs_zone_info *zone_info;
	int zone_opened_count;
	struct xcpfs_zone_info **zone_opened;
	int zone_active_count;
	struct xcpfs_zone_info **zone_active;
};

//理论上这里应该有着完整的cache机制，但是为了实现的简单，基本约等于无
struct nat_entry {
	bool pinned;
	bool dirty;
	int ino;
	int block_addr;
	struct list_head nat_link;
};

struct xcpfs_nat_info {
	// spinlock_t nat_info_lock;
	struct xcpfs_rwsem nat_info_rwsem;
	int cached_nat_count;
	struct list_head nat_list;
};

struct xcpfs_sb_info {
	struct super_block *sb;
	// struct xcpfs_zone_device_info *zone_device_info;
	int root_ino;
	int meta_ino;
	int node_ino;

	int nat_page_count;
	int zit_page_count;
	int ssa_page_count;

	struct xcpfs_zm_info *zm;
	struct xcpfs_nat_info *nm;
	struct inode *node_inode;
	struct inode *meta_inode;
};


struct xcpfs_inode_info {
	struct inode vfs_inode;
};

inline struct xcpfs_sb_info* XCPFS_SB(struct super_block *sb) {
	return sb->s_fs_info;
}



/*data.c*/
struct page* xcpfs_grab_page(struct super_block *sb, block_t iblock);
void xcpfs_free_page(struct page *page);

/*zone_mgmt.c*/
#define EMAXOPEN 1
#define EMAXACTIVE 2
#define EZONE 3
int xcpfs_zone_mgmt(struct super_block *sb,int zone_id,enum req_op op);

/*nat_mgmt.c*/
int insert_nat(struct super_block *sb, int nid, int blkaddr, bool pinned, bool dirty);
int remove_nat(struct super_block *sb, int nid);
int update_nat(struct super_block *sb, int nid,int new_blkaddr,bool pinned);
struct nat_entry *lookup_nat(struct super_block *sb, int nid);

/*data.c*/
enum page_type {
	META_NODE = 1,
	META_DATA,
	REG_NODE,
	REG_DATA,
	NR_PAGE_TYPE
}

struct xcpfs_path {
	int offset[]
};

/*submit之前需要完成ino，iblock，op*/
struct xcpfs_io_info {
	struct xcpfs_sb_info *sbi;
	struct bio *bio;
	/*
	如果是data page，则代表inode的ino；
	如果是node page，则代表nid，此时iblock等于nid
	*/
	nid_t ino;		
	loff_t iblock;

	block_t old_blkaddr;
	block_t new_blkaddr;

	struct page *dnode;

    struct page *page; //locked page 
    struct writeback_control *wbc;
    enum req_op op;
    enum page_type type;
    spinlock_t io_lock;
};

struct xcpfs_io_info *alloc_xio();
void free_xio(struct xcpfs_io_info *xio);

/*data.c*/
struct inode *xcpfs_iget(struct super_block *sb, nid_t ino);

/*meta.c*/
struct page *xcpfs_get_node_page(struct super_block *sb,nid_t nid);
#endif