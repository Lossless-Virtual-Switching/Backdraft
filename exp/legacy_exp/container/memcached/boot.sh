#!/bin/sh

# Backdraft Legacy Expriment 
# Memcached/Mutilate container boot script
#

if [ "$#" -lt 1 ]
then
  echo "expecting at least one argument: type"
  exit 1
fi

name=$1

iperf=0
shuffle=0
if [ "$#" -gt 2 ]
then
  if [ $2 = "iperf3" ]
  then
    iperf=1
    iperf_server_ip=$3
  elif [ $2 = "shuffle" ]
  then
    shuffle=1
    shift 2
  fi
fi

# wait until BESS adds VPort

client_ip=10.1.1.2  # should match with pipeline.bess
server_ip=10.1.1.1  # should match with pipeline.bess
iperf_server_port=5001
iperf_parallel=5

if [ "x$name" = "xclient" ]
then
  echo client
  sleep 10
  pid=0
  if [ $iperf -gt 0 ]
  then
    iperf3 -c $iperf_server_ip --bandwidth 5000M --udp -B $client_ip -p $iperf_server_port -t 60 -P $iperf_parallel --logfile /root/iperf3.log &
    pid=$!
  elif [ $shuffle -gt 0 ]
  then
    ./shuffle_client $@ &
    pid=$!
  fi
  ./mutilate -s $server_ip -t 30 -w 10 -W 5 -T 1 -c 1
  if [ $pid -gt 0 ]
  then
    wait $pid
  fi
else
  echo server
  sleep 5
  if [ $iperf -gt 0 ]
  then
    # start an iperf3 server
    iperf3 -s -B $server_ip -p $iperf_server_port &
  fi
  memcached -m 64 -u root -l $server_ip -t 4
fi

