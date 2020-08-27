#!/bin/bash

percentages=( 0 0.1 1 5 10 50 )
filename="slow_receiver_effect_results.txt"
if [ -f $filename ]; then
	echo $filename already exists rename or remove then try.
	exit 1
fi

for i in ${percentages[@]}; do
	echo "================ slow_by: $i =============" >> $filename
	./run_exp.py --ecn_t 150 --slow_by $i >> $filename 
	echo "=========================================" >> $filename
done
