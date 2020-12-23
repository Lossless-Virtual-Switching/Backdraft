#!/bin/sh
export RTE_SDK=`realpath /proj/uic-dcs-PG0/fshahi52/new/post-loom/code/dpdk`
echo RTE_SDK: $RTE_SDK

cur=`pwd`

if [ ! -d $RTE_SDK/x86_64-native-linuxapp-gcc ]; then
  echo "Building DPDK ..."
  cd $RTE_SDK
  make config T=x86_64-native-linuxapp-gcc
  make -j 10 install T=x86_64-native-linuxapp-gcc
fi

cd $cur

if [ -d build/ ]; then
  rm -r build/
fi

make
