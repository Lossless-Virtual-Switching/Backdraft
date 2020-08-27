#!/bin/bash

# TODO: Check this script.
# Note: this script may have been out dated check if the pipeline and the 
# runner script is what it should be. 

# Run test for comparison between BESS (with or without slow receiver),
# perflow queueing, and Backdraft.

file_name=result.txt
if [ -d $file_name ]; then
	echo file: $file_name already exists
fi

echo "*************************************************************************" >> $file_name
echo "bess 0" >> $file_name
echo "*************************************************************************" >> $file_name
./run_slow_receiver_exp.py bess 0 >> $file_name
echo "*************************************************************************" >> $file_name
echo "bess 40" >> $file_name
echo "*************************************************************************" >> $file_name
./run_slow_receiver_exp.py bess 40 >> $file_name
echo "*************************************************************************" >> $file_name
echo "bkdrft 0 (bp=0)" >> $file_name
echo "*************************************************************************" >> $file_name
./run_slow_receiver_exp.py bkdrft 0 >> $file_name
echo "*************************************************************************" >> $file_name
echo "bkdrft 40 (bp=0)" >> $file_name
echo "*************************************************************************" >> $file_name
./run_slow_receiver_exp.py bkdrft 40 >> $file_name
echo "*************************************************************************" >> $file_name
echo "bkdrft 0 (bp=1)" >> $file_name
echo "*************************************************************************" >> $file_name
./run_slow_receiver_exp.py bkdrft 0  bp >> $file_name
echo "*************************************************************************" >> $file_name
echo "bkdrft 40 (bp=1)" >> $file_name
echo "*************************************************************************" >> $file_name
./run_slow_receiver_exp.py bkdrft 40 bp >> $file_name
echo "*************************************************************************" >> $file_name
echo "done" >> $file_name
echo "*************************************************************************" >> $file_name

