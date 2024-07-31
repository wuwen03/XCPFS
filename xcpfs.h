#ifndef XCPFS_H
#define XCPFS_H

#ifndef __KERNEL__
#define __KERNEL__
#endif

// #define DEVELOP
// #ifdef DEVELOP
// #define CONFIG_BLK_DEV_ZONED
// #endif

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
#include<linux/blk_types.h>
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

// #define PAGE_SIZE 4096
#define PAGE_SIZE_BITS 12
// #define BLOCKS_PER_ZONE 
// #define PAGE_SIZE 4096
#define XCPFS_MAGIC 0x520264
typedef int block_t; /*block地址*/
typedef int nid_t;

int sector_to_block(sector_t sector);

// struct xcpfs_nat_entry;

struct xcpfs_nat_entry_sb {
	__le32 nid;
	struct ne {
		uint8_t version;
		__le32 ino;
		__le32 block_addr;
	} ne;
};

#define META_NAT_NR ((PAGE_SIZE - sizeof(struct xcpfs_super_block) + 8) \
/ sizeof(struct xcpfs_nat_entry_sb))
//on disk super block
struct xcpfs_super_block {
    __le32 magic;
    __le32 root_ino;
	__le32 meta_ino;
	__le32 node_ino;
	__le32 nat_page_count;
	__le32 zit_page_count;
	__le32 ssa_page_count;
	__le16 meta_nat_cnt;
	//inode(1),direct(2),indirect(2),double_indirect(1)
	struct xcpfs_nat_entry_sb meta_nat[];
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

// struct xcpfs_sit_entry;
// struct xcpfs_zit_entry;

#define ZONE_VALID_MAP_SIZE 64
#define ZIT_ENTRY_PER_BLOCK (PAGE_SIZE / sizeof(struct xcpfs_zit_entry))
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
	struct xcpfs_zit_entry entries[ZIT_ENTRY_PER_BLOCK];
} __packed;


//on disk nat entry
struct xcpfs_nat_entry {
	uint8_t version;
    __le32 ino;
	__le32 block_addr;
} __packed;
#define NAT_ENTRY_SIZE (sizeof(struct xcpfs_nat_entry))
#define NAT_ENTRY_PER_BLOCK (PAGE_SIZE/NAT_ENTRY_SIZE)
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
	
	int zone_id;
	enum blk_zone_cond cond;
	sector_t wp;
	sector_t start;
	
	bool dirty;
	uint8_t zone_type;
	int vblocks;
	unsigned long *valid_map;
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
	int nid;
	int ino;
	int block_addr;
	struct list_head nat_link;
};

struct xcpfs_nat_info {
	// spinlock_t nat_info_lock;
	struct xcpfs_rwsem nat_info_rwsem;
	int cached_nat_count;
	struct list_head nat_list;
	struct list_head free_nat;
	struct list_head cp_nat;
};
//下面的分割的单位是block
#define REG_NAT_START 6000
#define ZIT_START 600000
#define SSA_START 800000
struct xcpfs_cpc;
struct xcpfs_sb_info {
	struct super_block *sb;
	// struct xcpfs_zone_device_info *zone_device_info;
	int root_ino;
	int meta_ino;
	int node_ino;

	int nat_page_count;
	int zit_page_count;
	int ssa_page_count;
	int total_meta_paga_count;


	struct xcpfs_zm_info *zm;
	struct xcpfs_nat_info *nm;
	struct inode *node_inode;
	struct inode *meta_inode;

	spinlock_t meta_nat_lock;
	int meta_nat_cnt;
	struct list_head meta_nat;

	struct xcpfs_rwsem cp_sem;
	int cp_phase;
	struct xcpfs_cpc *cpc;
};


struct xcpfs_inode_info {
	struct inode vfs_inode;
};

struct xcpfs_sb_info* XCPFS_SB(struct super_block *sb);

struct xcpfs_io_info;

/*zone_mgmt.c*/
#define EMAXOPEN 1
#define EMAXACTIVE 2
#define EZONE 3
int xcpfs_zone_mgmt(struct super_block *sb,int zone_id,enum req_op op);\
int validate_blkaddr(struct super_block *sb, block_t blkaddr);
int invalidate_blkaddr(struct super_block *sb, block_t blkaddr);
void alloc_zone(struct xcpfs_io_info *xio);
int flush_zit(struct super_block *sb);

/*nat_mgmt.c*/
int insert_nat(struct super_block *sb, int nid, int ino, int blkaddr, bool pinned, bool dirty);
int remove_nat(struct super_block *sb, int nid);
int update_nat(struct super_block *sb, int nid,int new_blkaddr,bool pinned);
struct nat_entry *lookup_nat(struct super_block *sb, int nid);
struct nat_entry *alloc_free_nat(struct super_block *sb,bool is_meta);
int invalidate_nat(struct super_block *sb, int nid);
int flush_nat(struct super_block *sb);
int cp_append_nat(struct super_block *sb,struct nat_entry *ne);

/*xio.c*/
enum page_type {
	META_NODE = 1,
	META_DATA,
	REG_NODE,
	REG_DATA,
	SUPERBLOCK,
	NR_PAGE_TYPE
};

/*submit之前需要完成
必填:sbi,ino,iblock,op,op_flags,type,page
选填:wbc,unlock
*/
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

    struct page *page; //locked page 
	struct page *dpage;//for end_io of writing data block
    struct writeback_control *wbc;

    enum req_op op;
	blk_opf_t op_flags;

    enum page_type type;
	bool unlock;
};

struct xcpfs_io_info *alloc_xio(void);
void free_xio(struct xcpfs_io_info *xio);
int xcpfs_submit_xio(struct xcpfs_io_info *xio);

/*inode.c*/
int xcpfs_set_inode(struct inode *inode);
struct inode *xcpfs_iget(struct super_block *sb, nid_t ino);
extern const struct address_space_operations xcpfs_data_aops;
int xcpfs_getattr(struct mnt_idmap * idmap, const struct path* path,
    struct kstat* stat, u32 request_mask, unsigned int flags);
int xcpfs_update_inode_page(struct inode *inode);
struct inode *xcpfs_new_inode(struct mnt_idmap *idmap,const struct inode *dir,umode_t mode);

/*meta.c*/
enum page_type get_page_type(struct xcpfs_sb_info *sbi,int ino, loff_t iblock);
int get_path(int offset[4], int iblock);
struct page *get_node_page(struct super_block *sb,nid_t nid,bool create);
struct page *get_dnode_page(struct page *page,bool create,bool *need);

/*data.c*/
struct page* xcpfs_grab_page(struct super_block *sb, block_t block);
int xcpfs_append_page(struct super_block *sb, struct page *page, int zone_id);
void xcpfs_free_page(struct page *page);
int do_prepare_page(struct page *page, bool create);
struct page *__prepare_page(struct inode *inode, pgoff_t index, bool for_write, bool create, bool lock);
struct page *xcpfs_prepare_page(struct inode *inode, pgoff_t index, bool for_write, bool create);
int xcpfs_commit_write(struct page *page, int pos, int copied);
int write_single_page(struct page *page, struct writeback_control *wbc);

/*file.c*/
int xcpfs_truncate(struct inode *inode);

/*reg.c*/
extern const struct file_operations xcpfs_file_operations;
extern const struct inode_operations xcpfs_file_inode_operations;

/*checkpoint.c*/
struct xcpfs_cpc {
	int nat_ptr;
	bool restart;
	struct page *page;
	struct xcpfs_super_block *raw_sb;
};

int do_checkpoint(struct super_block *sb);

/*dir.c*/
#define XCPFS_MAX_FNAME_LEN 256
struct xcpfs_dentry {
	char name[XCPFS_MAX_FNAME_LEN];
	nid_t ino;
	int namelen;
};
extern const struct file_operations xcpfs_dir_operations;
extern const struct inode_operations xcpfs_dir_inode_operations;

/*super.*/
void dump_fs(struct super_block *sb);
extern const struct super_operations xcpfs_sops;
#endif