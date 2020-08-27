#!/bin/sh

echo need sudo
sudo echo access granted

count_queue=8
type=bkdrft
duration=600 # seconds

# Kill dpdk_netperf
sudo pkill dpdk_netperf

# Run dpdk_netperf client
sudo ./build/dpdk_netperf \
  -l5 \
  --vdev="virtio_user0,path=/tmp/tmp_vhost.sock,queues=$count_queue" \
  --no-pci \
  --file-prefix=m2 \
  -- UDP_CLIENT 192.168.1.3 192.168.1.2 5002 8001 $duration 8 $type $count_queue

