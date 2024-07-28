fio --ioengine=psync --direct=1 --filename=/dev/nvme0n1 --rw=write \
      --bs=64k --group_reporting --zonemode=zbd --name=seqwrite \
      --offset_increment=8z --size=8z --numjobs=14 --job_max_open_zones=1