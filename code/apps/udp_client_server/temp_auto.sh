type=bess
# ./../run_slow_receiver_exp.py $type 40 bp bessonly

# sudo ./build/slow_receiver_exp \
# 	-l3 \
# 	--file-prefix=m3 \
# 	--vdev="virtio_user3,path=/tmp/tmp_vhost0.sock,queues=3" \
# 	-- bess server 0 &
#
# sudo ./build/slow_receiver_exp \
# 	-l2 \
# 	--file-prefix=m4 \
# 	--vdev="virtio_user4,path=/tmp/tmp_vhost1.sock,queues=3" \
# 	-- bess client 2 192.168.1.10 10.0.1.2 &


# Test VPort Server
sudo ./build/udp_app \
	-l4 \
	--file-prefix=bessd-dpdk-prefix \
	--no-pci \
  --proc-type=secondary \
	-- vport=ex_vhost0 192.168.1.5 1 bess server 0 &
sleep 1

# Test VPort Client
sudo ./build/udp_app \
	-l4 \
	--file-prefix=bessd-dpdk-prefix \
  --proc-type=secondary \
  --no-pci \
	-- \
  vport=ex_vhost1 192.168.1.5 1 bess client 1 192.168.20.20:5003 1 10 5020 0
sleep 1


# sudo ./build/slow_receiver_exp \
# 	-l8 \
# 	--file-prefix=m2 \
# 	--vdev="virtio_user2,path=/tmp/ex_vhost2.sock,queues=3" \
# 	-- $type server 40 &
# sleep 1


# ./commands.sh client $1


# sudo ./build/slow_receiver_exp \
# 	-l16 \
# 	--file-prefix=m1 \
# 	--vdev="virtio_user1,path=/tmp/ex_vhost1.sock,queues=3" \
# 	-- bkdrft client 2 192.168.1.10 10.0.0.1 &
#
#
# sudo ./build/slow_receiver_exp \
# 	-l20 \
# 	--file-prefix=m3 \
# 	--vdev="virtio_user3,path=/tmp/ex_vhost3.sock,queues=3" \
# 	-- $type client 1 10.0.0.1

# sleep 11
# sudo pkill slow_receiver_e
