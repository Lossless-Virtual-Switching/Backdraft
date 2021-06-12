#!/bin/bash
if [ "$IPERF_SERVER_IP" = "" ]
then
  echo set IPERF_SERVER_IP before running this !
  exit 1
fi
base_port=3000
count=1
duration=15
if [ $# -gt 0 ]
then
	count=$1
fi
echo $count client will be available...
for i in `seq $count`
do
	taskset -c $((i*2)) iperf3 -c $IPERF_SERVER_IP --port $((base_port + i)) \
    --time $duration --parallel 1 &
	echo $((base_port + i))
done
wait
echo done
