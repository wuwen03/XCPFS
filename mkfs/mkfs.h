// typedef int __le32;
// typedef char uint8_t;

// #define PAGE_SIZE 4096

// #include<linux/fs.h>
#define __KERNEL__
#include<linux/stat.h>

#define PAGE_SIZE_BITS 12
// #define BLOCKS_PER_ZONE 
#define PAGE_SIZE 4096
#define XCPFS_MAGIC 0x520264
typedef int block_t; /*block地址*/
typedef int nid_t;

#define __packed __attribute__((__packed__))

struct xcpfs_nat_entry_sb {
	__le32 nid;
	struct ne {
		uint8_t version;
		__le32 ino;
		__le32 block_addr;
	} ne;
};

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
}__packed ;

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

#define XCPFS_MAX_FNAME_LEN 256
struct xcpfs_dentry {
	char name[XCPFS_MAX_FNAME_LEN];
	nid_t ino;
	int namelen;
};