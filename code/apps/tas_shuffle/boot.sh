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

instances=4
echo  count instances $instances
if [ "$type" = "client" ]; then
  # one_gig=1073741824
  # repeate=`echo $size / $one_gig  | bc`
  # leftover=`echo $size % $one_gig | bc`
  # while [ $repeate -gt 0 ]
  # do
  # done
  for i in `seq $instances`
  do
    LD_PRELOAD=$TAS_DIR/lib/libtas_interpose.so \
       /root/shuffle_client $size $count_flow $dst_ip $server_port &
    echo client connecting to $dst_ip $server_port
    server_port=$(($server_port + 1))
  done
  wait
elif [ "$type" = "server" ]; then
  for i in `seq $instances`
  do
    LD_PRELOAD=$TAS_DIR/lib/libtas_interpose.so \
      /root/shuffle_server $port &
    echo server listenning on port $port
    port=$(($port + 1))
  done
  wait
else
  echo "type variable is not supported (type=$type)"
fi

