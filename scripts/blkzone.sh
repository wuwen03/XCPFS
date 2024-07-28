echo $1
size=16384
if [ "$1" = "report" ]; then
    arg2=$2
    off=$((arg2*size))
    sudo blkzone report -o $off -l $size /dev/nvme0n1 
elif [ "$1" = "append" ]; then
    off=0
    if [ "$2" = "" ]; then
        off=$((arg2*size))
    fi
    echo  "hello world" | sudo nvme zns zone-append /dev/nvme0n1 -s $off -z 4k
fi