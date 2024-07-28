# sudo x86_64-softmmu/qemu-system-x86_64 \
#     -name "FEMU-ZNSSD-VM" \
#     -enable-kvm \
#     -cpu host \
#     -smp 16 \
#     -m 24G \
#     -device virtio-scsi-pci,id=scsi0 \
#     -device scsi-hd,drive=hd0 \
#     -drive file=$OSIMGF,if=none,aio=native,cache=none,format=qcow2,id=hd0 \
#     -device femu,devsz_mb=20480,femu_mode=3 \
#     -net user,hostfwd=tcp::18080-:22 \
#     -net nic,model=virtio \
#     -nographic \
#     -qmp unix:./qmp-sock,server,nowait 2>&1 | tee log

IMGDIR=/home/wuwen/images
OSIMGF=$IMGDIR/femu.qcow2
FEMU=/home/wuwen/ConfZNS/build-femu/x86_64-softmmu

sudo $FEMU/qemu-system-x86_64 \
    -name "FEMU-ZNSSD-VM" \
    -enable-kvm \
    -cpu host \
    -smp 8 \
    -m 4096 \
    -cdrom $IMGDIR/ubuntu-22.04.4-desktop-amd64.iso \
    -hda $OSIMGF \
    -device femu,devsz_mb=2048,femu_mode=3 \
    -net nic \
    -net tap,ifname=tap0,script=no \
    # -nographic
    # -net user,hostfwd=tcp::18080-:22 \
    # -net nic,model=virtio \
    # -qmp unix:./qmp-sock,server,nowait 2>&1 | tee log
    # # -nographic \
