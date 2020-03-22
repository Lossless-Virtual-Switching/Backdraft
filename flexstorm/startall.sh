#!/bin/bash

set -e

TOPOLOGY=${TOPOLOGY:-LOCAL}
ALLHOSTS=(`./hosts.awk -v TOPOLOGY=$TOPOLOGY worker.h`)

echo Deploying...

# FlexNIC deployment?
if [[ $TOPOLOGY == *FLEXNIC* ]]; then
    echo "FlexNIC deployment"
    rm -f /dev/shm/worker_*_inq_* /dev/shm/worker_*_outq_*
    ./flexnic & FLEXNIC=$!
fi

# Load them all, remember PIDs
for ((i=0; i<${#ALLHOSTS[@]}; i++)); do
    ./worker $i & TASK[$i]=$!
done

sleep 1

echo "--- START ---"

# Start them all
kill -USR1 ${TASK[@]}

sleep 10

echo "--- STOP ---"

# Kill them all
kill ${TASK[@]} $FLEXNIC
