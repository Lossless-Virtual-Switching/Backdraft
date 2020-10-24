#!/bin/sh

echo need sudo
sudo echo access granted

COUNT_QUEUE=8
type=bess
if [ ! -z $1 ]; then
	type=$1
fi
echo type $type

# Kill dpdk_netperf
sudo pkill dpdk_netperf

# secondary
# Run dpdk_netperf client
sudo ./build/dpdk_netperf \
	-l5 \
	--file-prefix=bessd-dpdk-prefix \
	--proc-type=auto \
	-- vport=ex_vhost1 UDP_CLIENT 10.0.0.1 10.0.0.3 10001 10003 10 8 $type $COUNT_QUEUE
# sudo ./build/dpdk_netperf \
# 	-l5 \
# 	-w 07:00.1 \
# 	--file-prefix=m2 \
# 	-- UDP_CLIENT 10.0.0.1 10.0.0.3 10001 10003 10 8 $type $COUNT_QUEUE

# Kill dpdk_netperf
sudo pkill dpdk_netperf
