# post_loom


#### Software Versions
I'm using `dpdk-stable-17.11.6` which seem to be LTS and used in BESS as the
default DPDK. Also the BESS is just the latest commit.

#### Good github issues for BESS
[Issue 1](https://github.com/NetSys/bess/issues/934)

#### Huge page config in the grub
`GRUB_CMDLINE_LINUX_DEFAULT="hugepagesz=2M hugepages=4096 default_hugepagesz=1G
hugepagesz=1G hugepages=10"` 

also add the following line in the `/etc/fstab` `nodev /mnt/huge_1GB hugetlbfs
pagesize=1GB 0 0` More importantly for 2M you should `/etc/fstab` `nodev
/mnt/huge_2MB hugetlbfs pagesize=2MB 0 0`.  Since 1G pages are automatically
mapped to the `/dev/hugepages`. Also make sure you have the
corresponding directory in `/mnt/huge_2MB`.



#### TestPMD Command
This command is just to see if DPDK is able to find vhost ports or not:

`sudo ./build/app/testpmd –l 0-1 –n 4
--vdev="virtio_user0,path=/users/alireza/my_vhost0.sock,queues=1" --log-level=8
--socket-mem 1024,1024 --no-pci -- -i --disable-hw-vlan --total-num-mbufs=1025`

This command is useful for testing rate limiters: 

`sudo ./testpmd -l 3-4
-n 4 --vdev="virtio_user0,path=/home/alireza/my_vhost0.sock,queues=1"
--vdev="virtio_user1,path=/home/alireza/my_vhost1.sock,queues=1" --log-level=8
--socket-mem 128 -- --portmask=0x3 --nb-cores=1 --forward-mode=io` 

#### TAS Command
`sudo ./tas/tas --ip-addr=10.0.0.1/24 --fp-cores-max=1
--fp-no-xsumoffload --fp-no-ints --fp-no-autoscale --dpdk-extra=--no-pci
--dpdk-extra=--vdev
--dpdk-extra=virtio_user0,path=/users/alireza/my_vhost0.sock,queues=1`


#### Tips on setting up
Please use `./bind.sh` at `post-loom/code/bess-v1/bin/bind.sh` # you might need
to change modify the script for interface name to bring down.

##### Sample TAS client command:
`sudo LD_PRELOAD=/proj/uic-dcs-PG0/post-loom/code/tas/lib/libtas_interpose.so
./echoserver_linux 1234 1 foo 10 500`

##### Sample TAS server command: 
`sudo LD_PRELOAD=/proj/uic-dcs-PG0/post-loom/code/tas/lib/libtas_interpose.so
./testclient_linux 10.0.0.1 1234 1 foo  /users/alireza/log_drop.txt 200 64 1`


## Environment Setup
### Ubuntu 16.04
1 ) install GCC 2) install packages 3) there is a grub jitter 4) benchmark 5) mellanox 6)docker build images 9)restart
### Ubuntu 18.04
1) install packages 2) grub 3) mellanox 4) docker build images 5) restart
