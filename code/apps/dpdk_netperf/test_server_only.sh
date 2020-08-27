#!/bin/sh

echo need sudo
sudo echo access granted

nic=0
if [ "x$1" = "xnic" ]; then
  nic=1
  echo connecting to nic...
fi

COUNT_QUEUE=8
type=bkdrft
# Kill dpdk_netperf
sudo pkill dpdk_netperf

if [ $nic -gt 0 ]; then
# Run dpdk_netperf server
sudo ./build/dpdk_netperf \
  -l4 \
  --file-prefix=m1 \
  -w 07:00.1 \
  -- UDP_SERVER 192.168.1.2 $COUNT_QUEUE $type
else
# Run dpdk_netperf server
sudo ./build/dpdk_netperf \
  -l4 \
  --file-prefix=m1 \
  --no-pci \
  --vdev="virtio_user1,path=/tmp/tmp_vhost.sock,queues=$COUNT_QUEUE" \
  -- UDP_SERVER 192.168.1.2 $COUNT_QUEUE $type 4
fi
