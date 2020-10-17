#!/bin/bash

# ============================================================
# TODO: This script should be configured with the host machine
# PCI and interface address
# ============================================================

# Bind Intel NIC for DPDK applications
# PCI: af:00.0
# Interface: ens3f0

curdir=`dirname $0`
curdir=`realpath $curdir`
dpdk_devbind=$curdir/../code/bess-v1/bin/dpdk-devbind.py
pci="af:00.0"
inet="ens3f0"

interface_desc=`ifconfig | grep $inet`
interface_res=$?
binding=0
if [ $interface_res -eq 0 ]
then
	binding=1
	echo interface is available. description:
	echo $interface_desc
	echo binding...
	echo "should continue (y/n)? "
	read cmd
	if [ "$cmd" != "y" ]
	then
		echo operation canceled
		exit 0
	fi
else
	echo interface is not available. Trying to unbind...
	echo "should continue (y/n)? "
	read cmd
	if [ "$cmd" != "y" ]
	then
		echo operation canceled
		exit 0
	fi
fi

if [ $binding -eq 1 ]
then
	echo binding...
	module="uio_pci_generic"

	sudo modprobe $module
	sudo ifconfig $inet down
	sudo $dpdk_devbind -b $module $pci
else
	echo unbinding...
	module="ixgbe"
	sudo $dpdk_devbind -u $pci
	sudo $dpdk_devbind -b $module $pci
	sudo ifconfig $inet up
	interface_desc=`ifconfig | grep $inet`
	echo interface should be bind to kernel driver:
	echo $interface_desc
fi
echo "done!"

