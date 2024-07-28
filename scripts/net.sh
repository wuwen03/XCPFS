# Install necessary tools in host
# sudo apt-get install uml-utilities

# Create a virtual network card
sudo modprobe tun
sudo tunctl -t tap0 -u wuwen

# Set the priviledge of the network card
sudo chmod 0666 /dev/net/tun

# Set a static IP address for the network card
# Do not be on the same network segment as the real IP address.
# You can use "ifconfig -a" to show current network configuration.
sudo ifconfig tap0 192.168.108.1 up

# Enable IP packet forwarding for the virtual machine
# sudo su
# echo 1 > /proc/sys/net/ipv4/ip_forward 
# exit
sudo modprobe nft_compat
sudo modprobe xt_MASQUERADE
sudo modprobe nf_conntrack
#sudo iptables -t nat -A POSTROUTING -j MASQUERADE
sudo iptables -t nat -A POSTROUTING -s localhost -d 192.168.108.2 -j MASQUERADE
cat /proc/sys/net/ipv4/ip_forward 
# Now you can use "ifconfig -a" to see the IP address of tap0 
#nft_compat,ip_tables,xt_MASQUERADE,nf_conntrack
#nft_chain_nat xt_MASQUERADE nf_nat nf_conntrack nf_defrag_ipv6 nf_defrag_ipv4 nft_compat nf_tables libcrc32c nfnetlink
