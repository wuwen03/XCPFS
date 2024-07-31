echo "-------touch test-------"
i=0
while [ $i -lt 10 ]
do
touch /mnt/zns/touch_test_$i.txt || exit 1
echo "touch touch_test_$i.txt pass"
i=$[$i+1]
done
echo "touch test pass"

echo "-------mkdir test-------"
i=0
while [ $i -lt 10 ]
do
mkdir /mnt/zns/mkdir_test_$i || exit 1
echo "mkdir mkdir_test_$i pass"
i=$[$i+1]
done
echo "mkdir test pass"

echo "-------cd test-------"
cd /mnt/zns
dep=0
while [ $dep -lt 10 ]
do
mkdir cd_test_$dep || exit 1
cd cd_test_$dep
echo "depth : $dep"
dep=$[$dep+1]
done
echo "cd test pass"

echo "-------dd test-------"
cd /mnt/zns
i=0
while [ $i -lt 10 ]
do
touch dd_test_$i
dd if="/dev/urandom" of="./dd_test_$i" bs="1M" count="3" || exit 1
i=$[$i+1]
done
echo "dd test pass"

echo "--------------------------"
echo "all test pass"