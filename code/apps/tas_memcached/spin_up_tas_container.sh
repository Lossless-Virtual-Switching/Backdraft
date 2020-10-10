#!/bin/bash

curdir=$(dirname $0)
curdir=$(realpath $curdir)

if [ $# -lt 13 ]; then
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
  echo " ============"
	echo "* param11: type: 'client' or 'server'"
	echo " == Client ==:"
	echo "* param12: dst_ip"
	echo "* param13: duration"
	echo "* param14: warm up"
	echo "* param15: wait before measure"
  echo "* param16: threads"
  echo "* parma17: connections"
	echo " == Server ==:"
	echo "* param12: memory"
	echo "* param13: threads"
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
# ========
app_type=${11}

app_arguments=""
if [ "$app_type" = "client" ]
then
  # Client
  dst_ip=${12}
  duration=${13}
  warmup_time=${14}
  wait_before_measure=${15}
  mutilate_threads=${16}
  connections=${17}

  app_arguments="--env dst_ip=$dst_ip \
	--env duration=$duration \
	--env warmup_time=$warmup_time \
	--env wait_before_measure=$wait_before_measure \
	--env threads=$mutilate_threads \
	--env connections=$connections"
elif [ "$app_type" = "server" ]
then
  # Server
  memcached_memory=${12}
  memcached_workers=${13}

  app_arguments="--env memory=$memcached_memory \
	--env threads=$memcached_workers"
else
  echo "expecting client or server for the type (arg 11)"
  echo "got: $app_type"
  exit 1
fi

# make sure there is a sub directory for sharing hugepage file system  with container
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
        $app_arguments \
	-d \
	--log-opt mode=non-blocking \
	--log-opt max-buffer-size=4m \
	--log-opt max-size="20m" \
        --network none \
	$container_image_name

