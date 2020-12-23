#!/bin/sh

curdir=`dirname $0`
export RTE_SDK=`realpath $curdir/../../bess-v1/deps/dpdk-19.11.1/`
export RTE_TARGET="build"

# build protobuf
if [ ! -d $curdir/protobuf/_compile ]
then
  mkdir $curdir/protobuf/_compile
fi
cwd=`pwd`
cd $curdir/protobuf/
protoc-c --c_out=./_compile/ bkdrft_msg.proto
cd $cwd

rm -r $curdir/build/

make
