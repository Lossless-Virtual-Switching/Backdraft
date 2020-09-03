#!/bin/sh

# Note: This script expects following environment variables to be defined
# === TAS ===
# socket_file: path to the shared *.sock
# ip: tas ip 
# cores: number of tas cores
# count_queues: number of queues for port
# prefix: dpdk file prefix
# command_data_queue
# === CLIENT or SERVER ===
# type: 'client' or 'server', determine what program to execute
# server_ip
# threads
# connections
# message_size

TAS_DIR=/root/post-loom/code/tas
TAS_BENCH=/root/post-loom/code/tas-benchmark

IP=$ip
CORES=$cores
QUEUES=$count_queues

# kill tas
pkill tas

flags=""
if [ ${command_data_queue} -gt 0 ]
then
  flags="--fp-command-data-queue"
fi

exec $TAS_DIR/tas/tas --dpdk-extra=--vdev \
	--dpdk-extra="virtio_user0,path=$socket_file,queues=$QUEUES" \
	--dpdk-extra=--file-prefix --dpdk-extra=$prefix \
	--dpdk-extra=--no-pci \
	--fp-no-xsumoffload \
	--fp-no-autoscale \
  --fp-no-ints \
	--ip-addr=$IP \
	--cc=dctcp-win \
	--fp-cores-max=$CORES \
  $flags &

# wait for tas server to be ready
sleep 3
echo TAS Server Is Up

if [ "$type" = "client" ]; then
	LD_PRELOAD=$TAS_DIR/lib/libtas_interpose.so \
		$TAS_BENCH/micro_unidir/unidir_linux \
		-i $server_ip -t $threads -c $connections -b $message_size
elif [ "$type" = "server" ]; then
	LD_PRELOAD=$TAS_DIR/lib/libtas_interpose.so \
		$TAS_BENCH/micro_unidir/unidir_linux \
    -t $threads -c $connections -b $message_size -r
else
	echo "type variable is not supported (type=$type)"
fi

