#!/bin/sh

# Note: This script expects following environment variables to be defined
# === TAS ===
# socket_file: path to the shared *.sock
# ip: tas ip
# cores: number of tas cores
# count_queues: number of queues for port
# prefix: dpdk file prefix
# command_data_queue
# ===============
# type: 'client' or 'server', determine what program to execute
# === CLIENT  ===
# size
# count_flow
# dst_ip
# server_port
# === SERVER ===
# port

TAS_DIR=/root/post-loom/code/tas
TAS_BENCH=/root/post-loom/code/tas-benchmark

# kill tas
pkill tas

flags=""
if [ ${command_data_queue} -gt 0 ]
then
  flags="--fp-command-data-queue"
fi

# start TAS engine
exec $TAS_DIR/tas/tas --dpdk-extra=--vdev \
  --dpdk-extra="virtio_user0,path=$socket_file,queues=$count_queues" \
  --dpdk-extra=--file-prefix --dpdk-extra=$prefix \
  --dpdk-extra=--no-pci \
  --fp-no-xsumoffload \
  --fp-no-autoscale \
  --fp-no-ints \
  --ip-addr=$ip \
  --cc=dctcp-win \
  --fp-cores-max=$cores \
  ${flags} &

# wait for tas server to be ready
sleep 3
echo TAS Server Is Up

# client params
cores=1
# dst_ip_port_pair=$(echo "$dst_ip_port_pair" | tr -d '"')
# echo $dst_ip_port_pair

if [ "$type" = "client" ]; then
  LD_PRELOAD=$TAS_DIR/lib/libtas_interpose.so \
     /root/shuffle_client $size $count_flow $dst_ip $server_port
elif [ "$type" = "server" ]; then
  LD_PRELOAD=$TAS_DIR/lib/libtas_interpose.so \
    /root/shuffle_server $port
else
  echo "type variable is not supported (type=$type)"
fi

