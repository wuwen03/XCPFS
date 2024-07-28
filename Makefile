obj-m+=xcpFS.o
xcpFS-y := checkpoint.o data.o dir.o file.o inode.o meta.o nat_mgmt.o reg.o super.o xcpfs.o xio.o zone_mgmt.o
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) -O modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

insmod:
	sudo insmod ./xcpFS.ko

rmmod:
	sudo rmmod xcpFS

mount:
	sudo mount -t xcpfs /dev/nvme0n1 /mnt/zns

umount:
	sudo umount /mnt/zns

local:
	make -C /home/wuwen/linux-6.5 M=$(shell pwd) -O modules

localclean:
	make -C /home/wuwen/linux-6.5 M=$(shell pwd) clean

trans:
	./scripts/trans.sh

build_mkfs:
	gcc ./mkfs/mkfs.c /usr/local/lib/x86_64-linux-gnu/libnvme.a -o ./mkfs/mkfs

FS:
	sudo blkzone finish /dev/nvme0n1
	sudo blkzone reset /dev/nvme0n1
	make build_mkfs
	sudo ./mkfs/mkfs

shortcut:
	make FS all insmod mount
