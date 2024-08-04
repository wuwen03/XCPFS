# fio --ioengine=psync --direct=1 --filename=/dev/nvme0n1 --rw=write \
#       --bs=64k --group_reporting --zonemode=zbd --name=seqwrite \
#       --offset_increment=8z --size=8z --numjobs=14 --job_max_open_zones=1

      # --filename=/mnt/zns/fio1:/mnt/zns/fio2:/mnt/zns/fio3:/mnt/zns/fio4 \
      # --filename_format="jobnum" \

fio   --ioengine=psync --direct=0 \
      --directory=/mnt/zns \
      --filename=fio1:fio2:fio2:fio4:fio5:fio6:fio7:fio8 \
      --rw=ranwrite \
      --bs=4k --group_reporting --zonemode=none --name=seqwrite \
      --offset_increment=0MB --size=128MB --numjobs=8 --fsync_on_close=1 --iodepth=16