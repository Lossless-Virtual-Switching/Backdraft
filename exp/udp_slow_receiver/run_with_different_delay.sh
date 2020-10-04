#!/bin/bash

count_flow=8
count_queue=8
duration=60


for name in "bp" # "lossy"
do
  # config flags
  if [ "$name" = "lossy" ]
  then
    flags=""
  elif [ "$name" = "bp" ]
  then
    flags="--bp --buffering"
  fi

  file_name="${name}_result.txt"
  for delay in 0 1000 2000 5000 10000 20000 100000 200000 # 0 20 100 500 1000
  do

    echo "*************************************************************************" >> $file_name
    echo "[$name] cycles/packet: $delay" >> $file_name
    echo "*************************************************************************" >> $file_name

    echo
    echo command:
    echo "./run_udp_test.py $count_queue bess $delay \
      --count_flow $count_flow \
      $flags \
      --duration $duration >> $file_name"
    echo

    ./run_udp_test.py $count_queue bess $delay \
      --count_flow $count_flow \
      $flags \
      --duration $duration >> $file_name

  done
  echo "*************************************************************************" >> $file_name
  echo "done" >> $file_name
  echo "*************************************************************************" >> $file_name
done

