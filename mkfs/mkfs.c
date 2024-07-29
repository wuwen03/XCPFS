#include<libnvme.h>
#include<stdio.h>
#include<fcntl.h>
#include<stdlib.h>
#include<string.h>
#include"mkfs.h"

int fd;
struct nvme_zns_mgmt_send_args send_args;
struct nvme_zns_append_args append_args;

void *alloc_page(void) {
    void *p = malloc(PAGE_SIZE);
    memset(p,0,PAGE_SIZE);
    return p;
}

void init_mkfs(void) {
    fd = open("/dev/nvme0n1",O_RDWR);
    // append_page = alloc_page();
}

int zone2zslba(int zoneid) {
    return zoneid * 8 * 1024 / 4;
}

int zone_append(int zoneid,int *result,void *page) {
    memset(&append_args,sizeof(struct nvme_zns_append_args),0);
    append_args.zslba = zone2zslba(zoneid);
    append_args.result = result;
    append_args.data = page;
    append_args.args_size = sizeof(struct nvme_zns_append_args);
    append_args.fd = fd;
    append_args.timeout = 10000;
    append_args.nsid = 1;
    append_args.data_len = PAGE_SIZE;
    append_args.nlb = 0;
    return nvme_zns_append(&append_args);
}

int zone_mgmt(int zoneid,int *result,enum nvme_zns_send_action zsa) {
    memset(&send_args,sizeof(struct nvme_zns_mgmt_send_args),0);
    send_args.args_size = sizeof(struct nvme_zns_mgmt_send_args);
    send_args.nsid = 1;
    send_args.zsa = zsa;
    send_args.result = result;
    send_args.slba = zone2zslba(zoneid);
    send_args.timeout = 10000;
    send_args.fd = fd;
    // send_args.data = alloc_page();
    // send_args.data_len = PAGE_SIZE;
    return nvme_zns_mgmt_send(&send_args);
}

int init_root() {
    struct xcpfs_dentry *dentry = alloc_page();
    struct xcpfs_inode *root = alloc_page();
    int lba,ret;
    root->i_mode = S_IFDIR | S_IRWXU | (S_IRGRP | S_IXGRP) | (S_IROTH | S_IXOTH);
    root->i_size = 2 * PAGE_SIZE;
    root->i_blocks = 2;
    root->i_atime = root->i_ctime = root->i_mtime = 0;
    root->i_uid = root->i_gid = 1000;
    root->i_links = 2;

    ret = zone_mgmt(6,NULL,NVME_ZNS_ZSA_OPEN);
    if(ret) {
        return -1;
    }

    dentry->ino = 3;
    dentry->namelen = strlen(".");
    strcpy(dentry->name,".");
    if(zone_append(6,&lba,dentry)) {
        return -1;
    }
    root->i_addr[0] = lba;
    printf(". lba : 0x%x\n",lba);

    dentry->ino = 3;
    dentry->namelen = strlen("..");
    strcpy(dentry->name,"..");
    if(zone_append(6,&lba,dentry)) {
        return -1;
    }
    root->i_addr[1] = lba;
    printf(".. lba : 0x%x\n",lba);

    ret = zone_mgmt(5,NULL,NVME_ZNS_ZSA_OPEN);
    if(ret) {
        return -1;
    }

    if (zone_append(5,&lba,root)) {
        return -1;
    }
    printf("root node lba : 0x%x\n",lba);
    return lba;
}

int init_meta() {
    struct xcpfs_inode *meta_inode = alloc_page();
    int ret,lba;
    meta_inode->i_mode = S_IFREG | S_IRWXU;
    meta_inode->i_size = 0;
    meta_inode->i_blocks = 0;
    meta_inode->i_uid = meta_inode->i_gid = 1000;
    meta_inode->i_links = 1;

    ret = zone_mgmt(3,NULL,NVME_ZNS_ZSA_OPEN);
    if(ret) {
        return -1;
    }
    ret = zone_append(3,&lba,meta_inode);
    if(ret) {
        return -1;
    }
    printf("meta_inode lba : 0x%x \n",lba);
    return lba;
}

int main() {
    init_mkfs();
    printf("fd:%d\n",fd);
    struct xcpfs_super_block *sb;
    int result = 520;
    int ret;
    int lba;
    sb = alloc_page();
    sb->magic = XCPFS_MAGIC;
    sb->meta_ino = 1;
    sb->node_ino = 2;
    sb->root_ino = 3;
    sb->nat_page_count = 1;
    sb->zit_page_count = 1;
    sb->ssa_page_count = 1;

    sb->meta_nat[0].nid = sb->root_ino;
    sb->meta_nat[0].ne.ino = sb->root_ino;
    sb->meta_nat[0].ne.block_addr = init_root();

    sb->meta_nat[1].nid = sb->meta_ino;
    sb->meta_nat[1].ne.ino = sb->meta_ino;
    sb->meta_nat[1].ne.block_addr = init_meta();
    zone_mgmt(0,NULL,NVME_ZNS_ZSA_OPEN);
    ret = zone_append(0,&result,sb);
    printf("ret:0x%x result:0x%x\n",ret,result);
    return 0;
}