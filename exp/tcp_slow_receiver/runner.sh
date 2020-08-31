#!/bin/sh

exp=bd
log_dir=./${exp}_slow
log_file=${log_dir}/${exp}_slow_

exp_duration=60
exp_warmup=60
count_exp=8

echo Experiment estimated time: $(( ( exp_duration + exp_warmup + 20 ) * count_exp )) seconds

if [ ! -d ${log_dir} ]
then 
  mkdir -p ${log_dir}
fi

for i in 0 1 10 20 30 40 45 50
do
  echo ==============================
  echo slow by $i%
  python3 run_exp.py rpc \
    --slow_by $i \
    --count_flow 8 \
    --count_queue 2 \
    --buffering \
    --pfq \
    --cdq \
    --bp \
    --duration $exp_duration \
    --warmup_time $exp_warmup \
    --client_log ${log_file}${i}.txt &
   wait $!
done
