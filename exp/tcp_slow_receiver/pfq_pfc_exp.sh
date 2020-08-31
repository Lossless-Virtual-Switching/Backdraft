#!/bin/sh

slow_by=4

base_result_dir=pfq_pfc_result
if [ -d $base_result_dir ]
then
  echo result directory already exists: $base_result_dir
  exit 1
fi
mkdir $base_result_dir

for i in 1 8 16 32
do
  result_dir=$base_result_dir/${i}_8
  mkdir $result_dir

  # pfc
  ./run_exp.py rpc --slow_by $slow_by --buffering --count_flow $i \
      --count_queue 1 --client_log  $result_dir/pfc.txt

  sleep 5

  # pfq
  ./run_exp.py rpc --slow_by $slow_by --buffering --pfq --count_flow $i \
      --count_queue 1 --client_log  $result_dir/pfq.txt

  sleep 5
done

echo done

