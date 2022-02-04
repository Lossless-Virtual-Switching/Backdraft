#!/bin/bash
if [ ! -d mutilate/ ]
then
	git clone https://github.com/erfan111/mutilate.git
fi

cd mutilate/
if [ ! -f mutilate ]
then
	sudo apt-get update
	sudo apt-get install --yes scons libevent-dev gengetopt libzmq3-dev
	scons
fi

latency_file=../latency.txt
warmup=10
duration=60
./mutilate -s 10.10.1.1 --binary --time $duration \
	--keysize=fb_key \
	--valuesize=fb_value \
	--iadist=fb_ia \
	--threads=4 \
	--connections=4 \
	--warmup=$warmup \
	--save=$latency_file &> log_mutilate.txt
