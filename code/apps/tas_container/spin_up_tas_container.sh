#!/bin/bash

curdir=$(dirname $0)
curdir=$(realpath $curdir)

if [ $# -lt 20 ]; then
	echo "useage $(basename $0): '< expecting 20 params as below >'"
	echo "== docker config ==:"
	echo "* param1: container-name"
	echo "* param2: image-name (prefered tas_container)"
	echo "* param3: task-set (cpu-set)"
	echo "* param4: how much cpu"
	echo " == TAS config ==:"
	echo "* param5: socket-file-path"
	echo "* param6: tas ip"
	echo "* param7: tas count cores"
	echo "* param8: tas count queues"
	echo "* param9: huge prefix"
	echo "* param10: tas use command data"
	echo " == Client or Server ==:"
	echo "* param11: type: 'client' or 'server'"
	echo "* param12: port"
	echo "* param13: count flow"
	echo "* param14: count ip port pair"
	echo "* param15: dst ip port pair (e.g. 10.0.0.1:1234 172.17.2.1:3532 ...)"
	echo "* param16: flow duration"
	echo "* param17: message per sec"
	echo "* param18: message size"
	echo "* param19: flow num msg"
        echo "* param20: count threads" 
	exit 1
fi

# Docker
container_name=$1
container_image_name=$2
container_taskset=$3
container_cpus=$4
# Tas
socket_file=$5
tas_ip=$6
tas_cores=$7
tas_queues=$8
tas_dpdk_prefix=$9
tas_cmd=${10}
# Client or Server
app_type=${11}
app_port=${12}
app_count_flow=${13}
app_count_ip_port=${14}
app_ip_port=${15}
app_flow_duration=${16}
app_message_per_sec=${17}
app_message_size=${18}
app_flow_num_msg=${19}
app_count_thread=${20}

if [ ! -d /dev/hugepages/$tas_dpdk_prefix ]; then
	sudo mkdir -p /dev/hugepages/$7
fi

# Expecting socket file path to be an absolute path
if [ ! "${socket_file:0:1}" = "/" ]; then  # TODO: this check fails under (sh) it is not POSIX
	echo "Expecting an absolute path for the socket file path (arg4)." 
	echo Received a relative path.
	echo path: $socket_file
	exit 1
fi
socket_name=$(basename $socket_file)

# I made tas_setup by hand #TODO: it is better to have a docker file
sudo docker run \
	--name=$container_name \
	--privileged \
	-v /dev/hugepages/$tas_dpdk_prefix:/dev/hugepages \
	-v $socket_file:/root/vhost/$socket_name \
	--cpuset-cpus=$container_taskset \
	--cpus $container_cpus \
	--env socket_file=/root/vhost/$socket_name \
	--env ip=$tas_ip \
	--env cores=$tas_cores \
	--env count_queues=$tas_queues \
	--env prefix=$tas_dpdk_prefix \
	--env command_data_queue=$tas_cmd \
	--env type=$app_type \
	--env port=$app_port \
	--env count_flow=$app_count_flow \
	--env count_ip_port_pair=$app_count_ip_port \
	--env dst_ip_port_pair=\""$app_ip_port"\" \
	--env flow_duration=$app_flow_duration \
	--env message_per_sec=$app_message_per_sec \
	--env message_size=$app_message_size \
	--env flow_num_msg=$app_flow_num_msg \
	--env app_cores=$app_count_thread \
	-d \
	--log-opt mode=non-blocking \
	--log-opt max-buffer-size=4m \
	--log-opt max-size="20m" \
	$container_image_name

