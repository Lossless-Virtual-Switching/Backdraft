./../run_slow_receiver_exp.py bkdrft 0 bessonly

sudo ./build/slow_receiver_exp \
	-l3 \
	--file-prefix=m3 \
	--vdev="virtio_user3,path=/tmp/tmp_vhost0.sock,queues=3" \
	-- bess server 0 &

sudo ./build/slow_receiver_exp \
	-l2 \
	--file-prefix=m4 \
	--vdev="virtio_user4,path=/tmp/tmp_vhost1.sock,queues=3" \
	-- bess client 2 192.168.1.10 10.0.1.2 &


./commands.sh server $1 &
sleep 1
./commands.sh client $1

# sleep 11
# sudo pkill slow_receiver_e
