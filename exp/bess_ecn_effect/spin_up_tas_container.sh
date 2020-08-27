curdir=$(dirname $0)
curdir=$(realpath $curdir)

if [ $# -lt 9 ]; then
	echo "useage $(basename $0): '< expecting 9 params as below >'"
	echo "* param1: container-name"
	echo "* param2: image-name (prefered tas_container)"
	echo "* param3: task-set (cpu-set)"
	echo "* param4: socket-file-name"
	echo "* param5: type: 'client' or 'server'"
	echo "* param6: tas ip"
	echo "* param7: huge prefix"
	echo "* param8: how much cpu"
	echo "* param9: server port"
	echo "* param10: count flow"
	exit 1
fi

if [ ! -d /dev/hugepages/$7 ]; then
	sudo mkdir -p /dev/hugepages/$7
fi

# I made tas_setup by hand #TODO: it is better to have a docker file
sudo docker run \
	--name=$1 \
	--privileged \
	-v /dev/hugepages/$7:/dev/hugepages \
	-v $curdir/tmp_vhost:/root/vhost \
	--cpuset-cpus=$3 \
	--env socket_file=/root/vhost/$4 \
	--env type=$5 \
	--env cores=2 \
	--env ip=$6 \
	--env prefix=$7 \
	--env count_queues=8 \
	--env server_port=$9 \
	--env count_flow=${10} \
	--cpus $8 \
	-d \
	$2

# --entrypoint /root/boot.sh \
# --net=none \
# -i \
# -d \
# --entrypoint /bin/bash \
#  tail -f /dev/null
