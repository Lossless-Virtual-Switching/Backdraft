#!/bin/bash
logfile=./udp_client_log.txt
duration=70
parallel=1
iperf3 --logfile $logfile -c 10.10.1.1 -u -b 2M -t $duration

