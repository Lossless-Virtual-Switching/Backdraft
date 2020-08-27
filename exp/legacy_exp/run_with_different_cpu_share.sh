#!/bin/bash

exp=memcached
mkdir -p results/$exp/
output_file=results/$exp/cpu_share_

for i in "1.0" "0.999" "0.990" "0.90" "0.80" "0.70" "0.60" "0.50"
do
  echo slow by $i
  exec ../multi_run.py ./run_exp.py "$exp --cpus $i" ${output_file}$i.txt &
  wait $!
done

