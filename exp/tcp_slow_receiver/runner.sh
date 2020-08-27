#!/bin/sh

exp=bp
log_dir=./${exp}_slow
log_file=${log_dir}/${exp}_slow_

if [ ! -d ${log_dir} ]
then 
  mkdir -p ${log_dir}
fi

for i in 0 "0.1" 1 10 20 30 40 50
do
  echo slow by $i%
  python3 run_exp.py \
    --slow_by $i \
    --count_flow 2 \
    --count_queue 1 \
    --buffering \
    --bp \
    --duration 20 \
    --client_log ${log_file}${i}.txt &
   wait $!
done
