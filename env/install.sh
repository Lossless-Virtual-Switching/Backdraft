#!/bin/bash

gcc_version=$(gcc -dumpversion)
expected_gcc_version=7

if [ "$gcc_version" != "$expected_gcc_version" ]; then
	echo expecting gcc version of $expected_gcc_version found: $gcc_version
	exit 1
fi

sudo apt-get update -y

# BESS depencendcy
sudo apt install -y make apt-transport-https ca-certificates g++ make pkg-config libunwind8-dev liblzma-dev zlib1g-dev libpcap-dev libssl-dev libnuma-dev git python python-pip python-scapy libgflags-dev libgoogle-glog-dev libgraph-easy-perl libgtest-dev libgrpc++-dev libprotobuf-dev libc-ares-dev libgtest-dev cmake

# These two might fail (on ubuntu 16)
sudo apt-get install  protobuf-compiler-grpc libbenchmark-dev
ret=$?
if [ "$ret" != "0" ]; then
	echo failed to install protobuf-compiler-grpc libbenchmark-dev3
	echo you need to install google benchmark by compiling the source code!
	# exit 1
fi

python2 -m pip install --upgrade pip

python2 -m pip install --user protobuf grpcio scapy

# NUMA
sudo apt install -y numactl libnuma-dev

# Docker
sudo apt install -y docker docker-compose

#
sudo apt install -y python3 python3-pip
python3 -m pip install --upgrade pip
python3 -m pip install pyyaml

# Setup 1G huge pages
sudo echo 'GRUB_CMDLINE_LINUX_DEFAULT="default_hugepagesz=1G hugepagesz=1G hugepages=30"' >> /etc/default/grub
ret=$?
if [ "$ret" == "0" ]; then
	sudo update-grub
fi
