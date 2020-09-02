#!/bin/sh

# Usage: [arg1] [arg2]
# * arg1: experiment (lossy, bp, pfq, cdq, bd)
# * arg2: app (rpc, unidir, memcached)

if [ $# -gt 0 ]
then
  exp=$1
else
  exp=bd
fi

if [ $# -gt 1 ]
then
  app=$2
else
  app="rpc"
fi

exp_flags=""

if [ $exp = "bd" ]
then
  exp_flags="--count_queue 2 --buffering --pfq --cdq --bp "
elif [ $exp = "cdq" ]
then
  exp_flags="--cdq --buffering --count_queue 2 "
elif [ $exp = "pfq" ]
then
  exp_flags="--pfq --buffering --count_queue 1 "
elif [$exp = "bp" ]
then
  exp_flags="--bp --buffering --count_queue 1 "
elif [$exp = "lossy" ]
then
  exp_flags="--buffering --count_queue 1 "
fi

app_flags="--count_flow 8"
if [ $app = "memcached" ]
then
  app_flags="--count_flow 64"
fi
  

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
  python3 run_exp.py $app \
    --slow_by $i \
    $app_flags \
    $exp_flags \
    --duration $exp_duration \
    --warmup_time $exp_warmup \
    --client_log ${log_file}${i}.txt &
   wait $!
done
