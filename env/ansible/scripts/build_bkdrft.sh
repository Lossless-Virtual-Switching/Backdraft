#!/bin/bash

# Get current script directory
curdir=`dirname $0`
curdir=`realpath $curdir`

# Build BESS
bessdir=`realpath $curdir/../../../code/bess-v1/`
cd $bessdir
exec ./build.py bess &
wait $!
# TODO: make sure BESS was built successfully

libdir=`realpath $curdir/../../../code/lib/`
# Build bd_vport
bdvport_dir=$libdir/bd_vport/
cd $bdvport_dir
exec ./make.sh &
wait $!
# TODO: Make sure that bd_vport was built successfully

# Build bkdrft (c interface)
libbd_dir=$libdir/bkdrft/
cd $libbd_dir
exec ./make.sh &
wait $!
# TODO: Make sure libkdrft was built successfully

apps_dir=`realpath $curdir/../../../code/apps`
# Build udp_app
udpapp_dir=$apps_dir/udp_client_server/
cd $udpapp_dir
exec ./make.sh
wait $!
# TODO: Make sure udp app was built successfully

# Build burst composer
cd $apps_dir/burst_composer/
exec ./make.sh
wait $!
# TODO: Make sure burst_composer was built successfully

