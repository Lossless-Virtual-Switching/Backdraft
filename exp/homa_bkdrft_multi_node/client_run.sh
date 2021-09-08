#!/bin/bash
sudo pkill bessd
sudo pkill dpdk_test
sudo pkill dpdk_client_system_test
sudo rm /dev/shm/dpdk_test6 &> /dev/null
sudo rm /dev/shm/homa_openloop_dpdk_test &> /dev/null

# ./run_exp.py 1 2 client --drop="0.0" --vswitch_path ../../../homa-bess/bess/ \
# 	--pci 03:00.1 --count_queue 3 --queue_size 256 --time 10 \
# 	--tx_size 200 --delay 10000

./run_exp.py 1 2 client --drop="0.0" --vswitch_path ../../../homa-bess/bess/ \
	--pci 03:00.1 --count_queue 1 --queue_size 256 --time 10 \
	--tx_size 200 --delay 5000
