make module_install,在module里面生成文件
软连接build到源代码目录
tunctl如果找不到device,先删了，然后modprobe自己创建
femu宿主机的网络配置：
subnet：192.168.108.0/24；
address：192.168.108.2；
gateway：192.168.108.1；
nameserver：8.8.8.8,8.8.4.4,223.5.5.5,223.6.6.6;
iptables需要手动加载一些module，已经写进脚本里面了，不过ip_forward还需要手动一下
nat转发规则不能全部转全部，会出现vsc连不上wsl的情况
