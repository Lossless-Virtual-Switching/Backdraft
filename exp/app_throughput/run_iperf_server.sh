#!/bin/bash
base_port=3000
count=1
if [ $# -gt 0 ]
then
	count=$1
fi
echo $count servers will be available...
for i in `seq $count`
do
	taskset -c $((i*2)) iperf3 -s --port $((base_port + i)) &
	echo $((base_port + i))
done
wait
echo done
