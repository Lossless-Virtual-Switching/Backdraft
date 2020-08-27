#!/bin/bash

# TCP Slow Receiver Experiment
# Runs Two servers and a client tcp application (using tas container docker image)
# One of the servers has cpu usage limit acting as slow receiver system.
# Logs are gathered for further processing.

mkdir ./no_bp_log/
mkdir ./bp_log/

# The slow receiver will have less cpu in each iteration.
# it will have less cpu by `$slow_by` percent than it would normally would have
for slow_by in 0 1 2 4 8 16 32 50 64
do
  ./run_exp.py --slow_by $slow_by --client_log "./no_bp_log/log_no_bp_slow_by_${slow_by}.txt" >> no_bp_results.txt
  sleep 3
  ./run_exp.py --slow_by $slow_by --bp --buffering --client_log "./bp_log/log_bp_slow-by_${slow_by}.txt" >> bp_results.txt
done
