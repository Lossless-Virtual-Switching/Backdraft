#!/bin/bash

ssh simon@$1 "make -C mystorm clean >clean.log 2>&1; make -j12 -C mystorm TOPOLOGY=$TOPOLOGY >build.log 2>&1 || (cat build.log; exit 1)"; echo "$?" >> /tmp/retcodes.tmp
