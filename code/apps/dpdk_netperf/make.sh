#!/bin/bash

curdir=`dirname $0`
# export RTE_SDK=$(realpath $curdir/../../../code/dpdk)
export RTE_SDK=`realpath "$curdir/../../bess-v1/deps/dpdk-19.11.1/"`
export RTE_TARGET="build"
# export RTE_TARGET=x86_64-native-linuxapp-gcc

cur=`pwd`

if [ ! -d $RTE_SDK/$RTE_TARGET ]; then
  echo "BESS DPDK dependency not found, has BESS been compiled?"
  exit 1
	# echo "Building DPDK ..."
  # # TODO build folder may not be created!
	# cd $RTE_SDK
	# make config T=x86_64-native-linuxapp-gcc
	# make -j 10 install T=x86_64-native-linuxapp-gcc
fi

cd $cur

# if [ -d build/ ]; then
# 	rm -r build/
# fi
# make clean

# protobuf-c setup
# protoc-c --c_out=./utils/include/ protobuf/bkdrft_msg.proto

# echo "Start building applications ... " $RTE_SDK

make
