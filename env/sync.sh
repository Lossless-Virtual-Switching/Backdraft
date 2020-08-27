#!/bin/bash

path=`realpath $(dirname $0)`
root_path=`realpath $path/..`
echo $path
echo $root_path
args=""

if [ $# -eq 0 ]
  then
    echo "testing"
    args="-avurnh"
fi

if [ $# -eq 1 ]
  then
    echo "real"
    args="-avurh"
fi

cd $root_path

rsync $args --exclude='.git' \
    --exclude='code/bess-v1/core/bess.a' \
    --exclude='*.o' \
    --exclude='code/bess-v1/core/bessd' \
    --exclude='code/bess-v1/deps/dpdk-19.11.1/build/*' \
    --exclude='code/bess-v1/.deps' \
    --exclude='code/dpdk/x86_64-native-linux-gcc' \
    --exclude='code/dpdk/x86_64-native-linuxapp-gcc' \
    $root_path/ alireza@clnode098.clemson.cloudlab.us:/proj/uic-dcs-PG0/freshBD
