#!/bin/sh

slow_by=4

base_result_dir=pfq_pfc_result
if [ -d $base_result_dir ]
then
  echo result directory already exists: $base_result_dir
  exit 1
fi
mkdir $base_result_dir

# 1 8 32
for i in 16
do
  result_dir=$base_result_dir/${i}_8
  mkdir $result_dir

  # pfc
  ./run_exp.py --slow_by $slow_by --buffering --count_flow $i --client_log  $result_dir/pfc.txt

  sleep 5

  # pfq
  ./run_exp.py --slow_by $slow_by --buffering --pfq --count_flow $i --client_log  $result_dir/pfq.txt

  sleep 5
done

echo done

