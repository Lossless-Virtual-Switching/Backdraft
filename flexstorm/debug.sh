#!/bin/bash

WORKER=`pidof worker`

operf -p $WORKER -t -k /usr/lib/debug/boot/vmlinux-3.13.0-55-generic
