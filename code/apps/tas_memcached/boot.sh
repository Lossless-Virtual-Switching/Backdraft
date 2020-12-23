#!/bin/sh

# Note: This script expects following environment variables to be defined
# === TAS ===
# socket_file: path to the shared *.sock
# ip: tas ip
# cores: number of tas cores
# count_queues: number of queues for port
# prefix: dpdk file prefix
# command_data_queue
# === CLIENT  ===
# type: 'client' or 'server', determine what program to execute
# dst_ip
# duration
# warmup_time
# wait_before_measure
# threads
# connections
# === SERVER ===
# memory (in mb)
# threads

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
  --count-queue=$count_queues \
  ${flags} &

# wait for tas server to be ready
sleep 3
echo TAS Server Is Up

# client params
cores=1
mtcp_config=foo
result_file=/tmp/log_drop_client.txt
if [ -z "$message_size" ]; then
  message_size=200
fi
max_pending_flow=64
openall_delay=0

dst_ip_port_pair=$(echo "$dst_ip_port_pair" | tr -d '"')
echo $dst_ip_port_pair

if [ "$type" = "client" ]; then
  LD_PRELOAD=$TAS_DIR/lib/libtas_interpose.so \
     /root/mutilate/mutilate -s $dst_ip -t $duration -w $warmup_time \
     -W $wait_before_measure -T $threads -c $connections
  sleep 10
  echo Done!
elif [ "$type" = "server" ]; then
  LD_PRELOAD=$TAS_DIR/lib/libtas_interpose.so \
    memcached -m $memory -u root -l $ip -t $threads
else
  echo "type variable is not supported (type=$type)"
fi

