#!/bin/bash

slow_by=1 # percentage
./run_exp --slow_by $slow_by --bp 0 --cdq 0 --pfq 0  # BESS
./run_exp --slow_by $slow_by --bp 1 --cdq 0 --pfq 0  # BESS-Lossless
./run_exp --slow_by $slow_by --bp 0 --cdq 0 --pfq 1  # PFQ
# ./run_exp --slow_by $slow_by --bp 1 --cdq 0 --pfq 1  # PFQ-Lossless
# ./run_exp --slow_by $slow_by --bp 0 --cdq 1 --pfq 1  # PFQ-CDQ
./run_exp --slow_by $slow_by --bp 1 --cdq 1 --pfq 1  # BKDRFT

