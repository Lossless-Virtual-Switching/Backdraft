#!/bin/bash

# slow_by=1 # percentage
# ./run_exp rpc --slow_by $slow_by                              # BESS
# ./run_exp rpc --slow_by $slow_by --bp --buffering             # BESS-Lossless
# ./run_exp rpc --slow_by $slow_by --pfq 1 --buffering          # PFQ
# # ./run_exp rpc --slow_by $slow_by --bp --pfq 1  # PFQ-Lossless
# # ./run_exp rpc --slow_by $slow_by --cdq --pfq 1  # PFQ-CDQ
# ./run_exp rpc --slow_by $slow_by --buffering --bp --cdq --pfq  # BKDRFT

app="memcached"
# "lossy" "bp" 
for exp in "pfq" "cdq" "bd"
do
  ./runner.sh $exp $app
done
