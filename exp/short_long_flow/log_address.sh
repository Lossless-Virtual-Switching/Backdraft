#!/bin/bash
echo $(hostname) >> cluster_info.txt
echo $(ifconfig | grep -A 1 eno50) >> cluster_info.txt
echo =============== >> cluster_info.txt
