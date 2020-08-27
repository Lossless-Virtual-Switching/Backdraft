echo need sudo
sudo echo access granted

COUNT_QUEUE=3

if [ ! -z "$2" ]; then
	type=$2
else
	type=bkdrft
fi

if [ "$1" == "server" ]; then
# run server
sudo ./build/slow_receiver_exp \
	-l4 \
	--file-prefix=m1 \
	--vdev="virtio_user0,path=/tmp/ex_vhost0.sock,queues=$COUNT_QUEUE" \
	-- $type server 0
else
# run clinet
sudo ./build/slow_receiver_exp \
	-l16 \
	--file-prefix=m2 \
	--vdev="virtio_user2,path=/tmp/ex_vhost1.sock,queues=$COUNT_QUEUE" \
	-- $type client 2 192.168.1.10 10.0.1.2
fi
