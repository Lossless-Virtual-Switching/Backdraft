#!/bin/sh
# Backdraft
# This script runs a simple client and server
# client generates udp packets and send them to the server
# server receive packets and free them. (for one directional test)

echo need sudo
sudo echo access granted

count_queue=8
type=bkdrft
duration=30

server_ip=192.168.1.2
client_ip=192.168.1.3

# Kill dpdk_netperf
sudo pkill dpdk_netperf

# Run dpdk_netperf server
sudo ./build/dpdk_netperf \
  -l7 \
  --no-pci \
  --file-prefix=m1 \
  --vdev="virtio_user1,path=/tmp/tmp_vhost2.sock,queues=$count_queue" \
  -- UDP_SERVER $server_ip $count_queue $type &

sleep 1

# Run dpdk_netperf client
sudo ./build/dpdk_netperf \
  -l5 \
  --no-pci \
  --vdev="virtio_user0,path=/tmp/tmp_vhost.sock,queues=$count_queue" \
  --file-prefix=m2 \
  -- UDP_CLIENT $client_ip $server_ip 50000 8001 $duration 8 $type $count_queue

sleep 2

sudo pkill dpdk_netperf

