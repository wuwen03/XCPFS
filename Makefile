obj-m+=xcpFS.o
xcpFS-y := checkpoint.o data.o dir.o file.o inode.o meta.o nat_mgmt.o reg.o super.o xcpfs.o xio.o zone_mgmt.o
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) -O modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean