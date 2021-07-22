#!/bin/bash
# ping -c 3 192.168.1.10
sudo pkill bessd
sudo pkill dpdk_test
sudo pkill dpdk_server_sys
./run_exp.py 1 1 server --drop "0.0" --vswitch_path ../../../homa-bess/bess/ --pci 41:00.0 --count_queue 1 --queue_size 256 --time 1000 --tx_size 1000  # --slow-down 100000 
