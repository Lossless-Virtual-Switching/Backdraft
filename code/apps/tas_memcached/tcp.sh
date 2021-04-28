#!/bin/bash
noslow=""
#noslow="--no-slow"
LD_PRELOAD=/root/post-loom/code/tas/lib/libtas_interpose.so \
  /root/post-loom/code/tas-benchmark/micro_rpc_modified/slow_echoserver_linux \
  --thread 1 \
  --max-bytes 64 \
  --max-flows 4096 \
  $noslow \
  --dpriod 15 \
  --dduration 15 \
  --ipriod 1000 \
  --iduration 1000 \
  1404
