#!/bin/sh

# Backdraft Legacy Expriment 
# ApacheBench container boot script
#

if [ "$#" -lt 1 ]
then
  echo "expecting at least one argument: hostname (the usage is same as 'ab')"
  exit 1
fi

# wait until BESS adds VPort
sleep 15

# while true
# do
#   sleep 1000
# done

# it is important to use -B argument for binding to correct interface
ab $@ &
wait $!
