#!/bin/bash
sudo pkill bessd
sudo pkill dpdk_test
sudo pkill dpdk_client_system_test
./run_exp.py 1 1 client --drop="0.0" --pci 41:00.0 --count_queue 1 --queue_size 256 --time 10 --tx_size 1000
