#ifndef DATA_H
#define DATA_H

#include<linux/bio.h>
#include"xcpfs.h"
struct xcpfs_path {
    int offset[4];
};
static void xcpfs_read_end_io(struct bio *bio);
static int submit_node_xio(struct xcpfs_io_info *xio);
#endif