#!/bin/bash
if [ $# -lt 2 ]
then
  echo "expecting two argument: type server_ip"
  echo "* type: (client or server)"
  echo "* server_ip"
  exit 1
fi

type=$1
cpu_list="0"  # !! not working

port_num=5001
prallel=1
client_ip=10.1.1.2  # should match with pipeline.bess
exp_duration=30

if [ "x$type" = "xclient" ]
then
  echo client
  sleep 10
  shift  # remove type arg from the list
  pids=()
  echo "$@"
  for server_ip in $@
  do
    # $server_ip should match with pipeline.bess
    echo running a iperf3 with destination: $server_ip
    exec iperf3 -c $server_ip -B $client_ip -t $exp_duration -p $port_num -P $prallel -M 1460 -l 64536 &  # -R
    pids+=($!)
  done
  # wait for clients to finish
  for pid in ${pids[@]}
  do
    wait $pid
  done
else
  server_ip=$2  # should match with pipeline.bess
  echo server: $server_ip
  sleep 5
  # taskset -c $cpu_list
  iperf3 -s -B $server_ip  -p $port_num &
  server_pid=$!

  # TODO: run sysbench (used in introduction of the paper)
  # sysbench should conflict with receiver process
  # taskset -c $cpu_list
  # sysbench --test=cpu --cpu-max-prime=20000000 run
  sleep 3

  wait $server_pid
fi
