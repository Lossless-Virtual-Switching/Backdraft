#!/bin/bash
ubuntu_version=`lsb_release -r -s`
ofed_version="5.3-1.0.0.1"
ofed_version="4.9-3.1.5.0"
ofed_arch="x86_64"
ofed_name="MLNX_OFED_LINUX-$ofed_version-ubuntu$ubuntu_version-$ofed_arch"
curdir=`dirname $0`

# cd $curdir/../deps/
cd /tmp/

if [ ! -d $ofed_name/ ]
then
	if [ ! -f $ofed_name.tgz ]
	then
		# https://content.mellanox.com/ofed/MLNX_OFED-5.3-1.0.0.1/MLNX_OFED_LINUX-5.3-1.0.0.1-ubuntu20.04-x86_64.tgz
		wget https://content.mellanox.com/ofed/MLNX_OFED-$ofed_version/$ofed_name.tgz
		if [ "$?" -ne "0" ]
		then
			exit 1
		fi
	fi
	tar -xzf $ofed_name.tgz
fi
cd $ofed_name/

sudo apt-get update && sudo apt-get install -y libmnl-dev

# sudo bash -c 'yes | ./mlnxofedinstall --all'
sudo bash -c 'yes | ./mlnxofedinstall --dpdk --upstream-libs'

echo If all the things went right, then reboot the system...
