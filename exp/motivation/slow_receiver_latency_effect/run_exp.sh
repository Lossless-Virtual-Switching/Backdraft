#!/bin/bash

core_limit=64
if [ $# -lt 1 ]
then
	echo "Usage: run_exp.sh <bg_value_size>"
	exit 1
fi

bg_val_size=$1
# echo "bg value sz $bg_val_size"

printf "\n\n"
cores=1
while [ $cores -le $core_limit ]
do
	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	inp="$bg_val_size\n$cores"
	printf $inp | ./run_memcached_exp.sh
	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	cores=$((cores * 2))
	sleep 5
done
printf "\n\n"
echo "Done"
