#!/bin/bash

set -e

export TOPOLOGY=${TOPOLOGY:-SWINGOUT_BALANCED}

ALLHOSTS=(`./hosts.awk -v TOPOLOGY=$TOPOLOGY worker.h`)
ALLHOSTS_DPDK=(`./hosts.awk -v TOPOLOGY=$TOPOLOGY -v DPDK=1 worker.h`)
HOSTS=`./hosts.awk -v TOPOLOGY=$TOPOLOGY worker.h | sort | uniq`

declare -A BONDING

# swingout3
BONDING["192.168.26.8"]="--vdev 'eth_bond0,mode=0,slave=0000:0c:00.0,slave=0000:0c:00.1'"
# swingout5
BONDING["192.168.26.20"]="--vdev 'eth_bond0,mode=0,slave=0000:08:00.0,slave=0000:08:00.1'"

buildall ()
{
    echo Building on $HOSTS...

# Build all
    for h in $HOSTS; do
	scp -q -r * simon@$h:~/mystorm &
    done

    wait

    rm -f /tmp/retcodes.tmp

    for h in $HOSTS; do
	./build.sh $h &
    done

    wait
    grep -qv 0 /tmp/retcodes.tmp && exit 1
    echo done
}

deployall ()
{
    echo Deploying on ${ALLHOSTS[*]}...

# Load flexnic process if FLEXNIC is part of config
    if [[ $TOPOLOGY == *FLEXNIC* ]]; then
	if [[ $TOPOLOGY == *DPDK* ]]; then
	    echo "DPDK deployment on $HOSTS"
	    for h in $HOSTS; do
		case $h in
		    128.208.6.106|192.168.26.22)
			ssh -q -tt simon@$h "sudo rm -f /dev/shm/worker_*_inq_* /dev/shm/worker_*_outq_*; ulimit -c unlimited; sudo ./mystorm/flexnic -l 16-31 -n 2 -- $h 2>&1" &
			;;
		    *)
			ssh -q -tt simon@$h "sudo rm -f /dev/shm/worker_*_inq_* /dev/shm/worker_*_outq_*; ulimit -c unlimited; sudo ./mystorm/flexnic -l 6-11 -n 2 -- $h 2>&1" &
		    # ssh -q -tt simon@$h "sudo rm -f /dev/shm/worker_*_inq_* /dev/shm/worker_*_outq_*; ulimit -c unlimited; sudo ./mystorm/flexnic -l 6-11 -n 2 ${BONDING[$h]} -- $h 2>&1" &
			;;
		esac
	    done
	else
	    echo "FlexNIC deployment"
	    for h in $HOSTS; do
		case $h in
		    128.208.6.106|192.168.26.22)
			ssh -q -tt simon@$h "sudo rm -f /dev/shm/worker_*_inq_* /dev/shm/worker_*_outq_*; ulimit -c unlimited; numactl -m 6,7 -N 6,7 ./mystorm/flexnic $h 2>&1" &
			;;
		    *)
			ssh -q -tt simon@$h "sudo rm -f /dev/shm/worker_*_inq_* /dev/shm/worker_*_outq_*; ulimit -c unlimited; taskset -a -c 6-11 ./mystorm/flexnic $h 2>&1" &
		esac
	    done
	# ssh -q -tt simon@${ALLHOSTS[0]} "rm -f /dev/shm/worker_*_inq_* /dev/shm/worker_*_outq_*; ulimit -c unlimited; ./mystorm/flexnic 2>&1" &
	fi
    fi

    if [[ $TOPOLOGY == *FLEXTCP* ]]; then
	echo "FlexTCP deployment"
	for ((i=0; i<${#ALLHOSTS[@]}; i++)); do
	    ssh -q -tt simon@${ALLHOSTS[$i]} "ulimit -c unlimited; export HOSTNAME; IP=${ALLHOSTS_DPDK[$i]} /home/simon/flextcp/scripts/flexnic_app.sh env LD_PRELOAD=/home/simon/flextcp/sockets/flextcp_interpose.so ./mystorm/worker $i 2>&1" &
	done
	sleep 5
    elif [[ $TOPOLOGY == *MTCP* ]]; then
	echo "mTCP deployment"
	for ((i=0; i<${#ALLHOSTS[@]}; i++)); do
	    ssh -q -tt simon@${ALLHOSTS[$i]} "ulimit -c unlimited; export HOSTNAME; /home/simon/flextcp/scripts/mtcp_app.sh ./mystorm/worker_mtcp $i 2>&1" &
	done
	sleep 5
    else
# Load all
	for ((i=0; i<${#ALLHOSTS[@]}; i++)); do
	    case ${ALLHOSTS[$i]} in
		128.208.6.106|192.168.26.22)
		    ssh -q -tt simon@${ALLHOSTS[$i]} "ulimit -c unlimited; numactl -m $i -N $i ./mystorm/worker $i 2>&1" &
		    ;;
		*)
	    # ssh -q -tt simon@${ALLHOSTS[$i]} "ulimit -c unlimited; ./mystorm/worker $i 2>&1" &
		    ssh -q -tt simon@${ALLHOSTS[$i]} "taskset -a -c 0-5 ./mystorm/worker $i 2>&1" &
		    ;;
	    esac
	done
    fi
}

startall ()
{
    # for h in $HOSTS; do
    # 	ssh simon@$h "sudo killall -USR1 worker" &
    # done
    echo startall does nothing...
}

killall ()
{
    echo Killing all on $HOSTS... Ignore errors after this.

    for h in $HOSTS; do
	ssh simon@$h "sudo killall -9 -q worker flexnic kernel worker_mtcp || true"
    done
}

case $1 in
    build)
	buildall
	;;

    deploy)
	buildall
	deployall
	;;

    start)
	startall
	;;

    kill)
	killall
	;;

    "")
	# No arg - do everything
	buildall
	deployall
	# sleep 3
	sleep 10

	echo "--- START ---"
	startall
	sleep 20
	echo "--- STOP ---"

	killall
	;;
esac
