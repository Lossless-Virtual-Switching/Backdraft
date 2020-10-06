#!/bin/bash

curdir=$(dirname $0)
curdir=$(realpath $curdir)

if [ $# -lt 12 ]; then
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
	echo "* param12: size"
	echo "* param13: count_flow"
	echo "* param14: dst_ip"
	echo "* param15: server_port"
	echo " == Server ==:"
	echo "* param12: port"
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
  size=${12}
  count_flow=${13}
  dst_ip=${14}
  server_port=${15}

  app_arguments="--env size=$size \
	--env count_flow=$count_flow \
	--env dst_ip=$dst_ip \
	--env server_port=$server_port"
elif [ "$app_type" = "server" ]
then
  # Server
  port=${12}

  app_arguments="--env port=$port"
else
  echo "expecting client or server for the type (arg 11)"
  echo "got: $app_type"
  exit -1
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

