#!/bin/bash
sudo pkill bessd
sudo pkill dpdk_test
sudo pkill dpdk_client_system_test
./run_exp.py 1 15 client --drop="0.0" --vswitch_path ../../../homa-bess/bess/ --pci 41:00.0 --count_queue 15 --queue_size 256 --time 10 --tx_size 1000
