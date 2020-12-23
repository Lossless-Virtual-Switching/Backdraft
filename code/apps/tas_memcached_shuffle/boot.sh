#!/bin/bash

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
# shuffle_size
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

tmp_count_queue=1
if [ ${command_data_queue} -gt 0 ]
then
  tmp_count_queue=2
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

# TODO: take this as parameter
shuffle_count_flow=1 # should not be changed (only 1 is supported)
# shuffle_size=$((2 * 1024 * 1024 * 1024))
shuffle_server_port=5678
shuffle_instance=1
shuffle_pids=()

mem_value_size=200
# mem_query_per_sec=1000
# count_mutilate_worker=4

if [ "$type" = "client" ]; then
  # start shuffle client
  for i in `seq $shuffle_instance`
  do
    LD_PRELOAD=$TAS_DIR/lib/libtas_interpose.so \
      /root/shuffle_client $shuffle_size $shuffle_count_flow \
                           $dst_ip $shuffle_server_port &
    shuffle_pids+=("$!")
  done
  sleep 1

  LD_PRELOAD=$TAS_DIR/lib/libtas_interpose.so \
     /root/mutilate/mutilate -s $dst_ip -t $duration -w $warmup_time \
     -W $wait_before_measure -B -T $threads -c $connections -V $mem_value_size &
  mutilate_pid=$!

  # wait for mutilate to finish
  wait $mutilate_pid

  # wait for shuffle to finish
  for shuffle_pid in $shuffle_pids
  do
    wait $shuffle_pid
  done

  echo Done!
  sleep 10 # wait some time before stopping client TAS engine
elif [ "$type" = "server" ]; then
  # start shuffle server
  LD_PRELOAD=$TAS_DIR/lib/libtas_interpose.so \
    /root/shuffle_server $shuffle_server_port &
  shuffle_pid=$!

  # start memcached
  LD_PRELOAD=$TAS_DIR/lib/libtas_interpose.so \
    memcached -m $memory -u root -l $ip -t $threads &
  wait $!

  wait $shuffle_pid
else
  echo "type is not supported (type=$type)"
fi

