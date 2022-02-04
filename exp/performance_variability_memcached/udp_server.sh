#!/bin/bash
logfile=./udp_server_log.txt
iperf3 --logfile $logfile -s -D 
