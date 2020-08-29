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
# port: port
# count_flow
# count_ip_port_pair
# dst_ip_port_pair e.g. "10.0.0.1:1234 10.0.0.2:5678 ..."
# flow_duration:
# message_per_sec: for client, how many message client can send per second
# message_size
# flow_num_msg
# app_cores


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


# Log execution command
echo $TAS_DIR/tas/tas --dpdk-extra=--vdev \
  --dpdk-extra="virtio_user0,path=$socket_file,queues=$QUEUES" \
  --dpdk-extra=--file-prefix --dpdk-extra=$prefix \
  --dpdk-extra=--no-pci \
  --fp-no-xsumoffload \
  --fp-no-autoscale \
  --fp-no-ints \
  --ip-addr=$IP \
  --cc=dctcp-win \
  --fp-cores-max=$CORES \
  ${flags} &

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
  ${flags} &  # 1> /dev/null 2>&1 


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
  echo LD_PRELOAD=$TAS_DIR/lib/libtas_interpose.so \
    $TAS_BENCH/micro_rpc_modified/testclient_linux \
    $count_ip_port_pair $dst_ip_port_pair $app_cores \
    $mtcp_config $result_file $message_size $max_pending_flow \
    $count_flow $flow_duration $message_per_sec $openall_delay \
    $flow_num_msg

  LD_PRELOAD=$TAS_DIR/lib/libtas_interpose.so \
    $TAS_BENCH/micro_rpc_modified/testclient_linux \
    $count_ip_port_pair $dst_ip_port_pair $app_cores \
    $mtcp_config $result_file $message_size $max_pending_flow \
    $count_flow $flow_duration $message_per_sec $openall_delay \
    $flow_num_msg
elif [ "$type" = "server" ]; then
  LD_PRELOAD=$TAS_DIR/lib/libtas_interpose.so \
    $TAS_BENCH/micro_rpc_modified/echoserver_linux $port $cores $mtcp_config $count_flow $message_size 
else
  echo "type variable is not supported (type=$type)"
fi

