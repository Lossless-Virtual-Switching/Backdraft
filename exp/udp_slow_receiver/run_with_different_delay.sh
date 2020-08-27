#!/bin/bash
file_name=no_bp_result.txt

for duration in 0 1 2 4 8 16 32 64 128 256
do

  echo "*************************************************************************" >> $file_name
  echo "bkdrft $duration" >> $file_name
  echo "*************************************************************************" >> $file_name
  ./run_slow_receiver_exp.py bkdrft $duration >> $file_name

done 

echo "*************************************************************************" >> $file_name
echo "done" >> $file_name
echo "*************************************************************************" >> $file_name

# ============================================================================

file_name=bp_result.txt

for duration in 0 1 2 4 8 16 32 64 128 256
do

  echo "*************************************************************************" >> $file_name
  echo "bkdrft $duration bp" >> $file_name
  echo "*************************************************************************" >> $file_name
  ./run_slow_receiver_exp.py bkdrft $duration bp >> $file_name

done 

echo "*************************************************************************" >> $file_name
echo "done" >> $file_name
echo "*************************************************************************" >> $file_name

