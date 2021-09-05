#!/bin/bash
# ping -c 3 192.168.1.10
sudo pkill bessd
sudo pkill dpdk_test
sudo pkill dpdk_server_sys

if (( $# != 1 ))
then
        printf "USAGE: $0 <slow_down>\n"
        exit
fi

./run_exp.py 1 2 server --drop "0.0" --vswitch_path ../../../homa-bess/bess/ \
        --pci 03:00.1 --slow-down $1 --count_queue 3 --queue_size 256 \
        --time 200 --tx_size 1000  # --slow-down 100000 

# ./run_exp.py 1 2 server --drop "0.0" --vswitch_path ../../../homa-bess/bess/ \
#         --pci 03:00.1 --slow-down $1 --count_queue 1 --queue_size 256 \
#         --time 120 --tx_size 1000  # --slow-down 100000 
