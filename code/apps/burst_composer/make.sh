#!/bin/sh
curdir=`dirname $0`
# export RTE_SDK=`realpath $curdir/../../dpdk`
export RTE_SDK=`realpath $curdir/../../../code/bess-v1/deps/dpdk-19.11.1/`
echo RTE_SDK: $RTE_SDK

# cur=`pwd`
# 
# if [ ! -d $RTE_SDK/x86_64-native-linuxapp-gcc ]; then
#   echo "Building DPDK ..."
#   cd $RTE_SDK
#   make config T=x86_64-native-linuxapp-gcc
#   make -j 10 install T=x86_64-native-linuxapp-gcc
# fi
# 
# cd $cur
sudo pkill burst_composer

echo make clean
make clean
echo
echo ========== make clean done ==============
echo
echo make
make
