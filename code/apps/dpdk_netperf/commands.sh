#!/bin/sh

echo need sudo
sudo echo access granted

COUNT_QUEUE=8

# Kill dpdk_netperf
sudo pkill dpdk_netperf

# Run dpdk_netperf server
sudo ./build/dpdk_netperf \
	-l4 \
	--file-prefix=m1 \
	--vdev="virtio_user1,path=$HOME/my_vhost1.sock,queues=$COUNT_QUEUE" \
	-- UDP_SERVER 192.168.1.2 $COUNT_QUEUE &

sleep 3

# Run dpdk_netperf client
sudo ./build/dpdk_netperf \
	-l5 \
	--vdev="virtio_user0,path=$HOME/my_vhost0.sock,queues=$COUNT_QUEUE" \
	--file-prefix=m2 \
	-- UDP_CLIENT 192.168.1.3 192.168.1.2 50000 8001 10 8 bkdrft $COUNT_QUEUE

# Kill dpdk_netperf
sleep 10
sudo pkill dpdk_netperf
