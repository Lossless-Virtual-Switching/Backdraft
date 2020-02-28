#!/bin/bash

for i in `seq 100 -2 -0`; do
	filename="/home/alireza/Documents/post-loom/exp/results/latency_0_"
	filename=$filename$i
	cat $filename".txt" | grep "latency" | awk '{print($5)}'
	#echo $i
done
