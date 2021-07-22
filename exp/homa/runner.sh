#!/bin/bash
if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "./runner <mode>" 
    echo "Help ------> " 
    echo "mode: server | client" 
    exit
fi
ping -c 3 192.168.1.10
./run_exp.py --vswitch_path ../../../homa-bess/bess/ 1 1 $1 --pci 41:00.0 --count_queue 1 --queue_size 256 --time 30 --tx_size 1000  # --slow-down 100000 
