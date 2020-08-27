#!/bin/bash

modprobe vfio-pci
echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode
ifconfig eno2 down
./dpdk-devbind.py --bind=vfio-pci 0000:81:00.1
