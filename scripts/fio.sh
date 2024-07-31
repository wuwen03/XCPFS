# fio --ioengine=psync --direct=1 --filename=/dev/nvme0n1 --rw=write \
#       --bs=64k --group_reporting --zonemode=zbd --name=seqwrite \
#       --offset_increment=8z --size=8z --numjobs=14 --job_max_open_zones=1

fio --ioengine=psync --direct=1 --filename=/mnt/zns/fio --rw=write \
      --bs=4k --group_reporting --zonemode=none --name=seqwrite \
      --offset_increment=8MB --size=64MB --numjobs=4 --job_max_open_zones=1 --direct=0 --fsync_on_close=1