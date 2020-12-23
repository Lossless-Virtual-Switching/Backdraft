#!/bin/sh

curdir=`dirname $0`
export RTE_SDK=`realpath $curdir/../../bess-v1/deps/dpdk-19.11.1/`
export RTE_TARGET="build"

make
