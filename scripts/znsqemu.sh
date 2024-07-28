IMGDIR=/home/wuwen/images
OSIMGF=$IMGDIR/femu.qcow2
FEMU=/home/wuwen/ConfZNS/build-femu/x86_64-softmmu
znsimg=/home/wuwen/images/zns.raw

sudo qemu-system-x86_64 \
    -name "FEMU-ZNSSD-VM" \
    -enable-kvm \
    -cpu host \
    -smp 8 \
    -m 4096 \
    -cdrom $IMGDIR/ubuntu-22.04.4-desktop-amd64.iso \
    -hda $OSIMGF \
    -device nvme,id=nvme0,serial=deadbeef,zoned.zasl=0 \
    -drive file=${znsimg},id=nvmezns0,format=raw,if=none \
    -device nvme-ns,drive=nvmezns0,bus=nvme0,nsid=1,logical_block_size=4096,\
physical_block_size=4096,zoned=true,zoned.zone_size=8M,zoned.\
zone_capacity=0,zoned.max_open=16,zoned.max_active=32,\
uuid=5e40ec5f-eeb6-4317-bc5e-c919796a5f79 \
    -net nic \
    -net tap,ifname=tap0,script=no \
    # -s -S
    # -device femu,devsz_mb=2048,femu_mode=3 \
    # -nographic
    # -net user,hostfwd=tcp::18080-:22 \
    # -net nic,model=virtio \
    # -qmp unix:./qmp-sock,server,nowait 2>&1 | tee log
    # # -nographic \
