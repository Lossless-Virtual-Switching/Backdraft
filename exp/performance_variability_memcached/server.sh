#!/bin/bash
# Tested on Ubuntu 18.04
memcached_version=`memcached --version`
if [ $? -ne 0 ]
then
	sudo apt update && sudo apt install memcached
fi

memcached -U 0 -l 10.10.1.1 -m 50000 -d -t 8
