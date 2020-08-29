#!/bin/sh

exp=pfq
log_dir=./${exp}_slow
log_file=${log_dir}/${exp}_slow_

exp_duration=60
exp_warmup=5
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
  python3 run_exp.py \
    --slow_by $i \
    --count_flow 8 \
    --count_queue 1 \
    --buffering \
    --pfq \
    --duration $exp_duration \
    --client_log ${log_file}${i}.txt &
   wait $!
done
