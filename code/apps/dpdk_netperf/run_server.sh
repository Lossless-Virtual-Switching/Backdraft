#!/bin/sh

echo need sudo
sudo echo access granted

COUNT_QUEUE=8
type=bess

# Kill dpdk_netperf
sudo pkill dpdk_netperf

# Run dpdk_netperf server
sudo ./build/dpdk_netperf \
	-l4 \
	-w 07:00.1 \
	--file-prefix=m1 \
	-- $type UDP_SERVER 10.0.0.3 $COUNT_QUEUE

