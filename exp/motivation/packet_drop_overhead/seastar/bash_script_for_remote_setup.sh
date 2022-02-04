#/bin/bash
(
SERVER=amd012.utah.cloudlab.us
USER=$USER
LOG=/tmp/_seastar_exp_log.txt
OUTDIR=./out/

function run_mutilate_master {
	(
	DURATION=30
	WARMUP=5
	TARGET=node1
	AGENTS="-a node3 -a node4 -a node5"
	./mutilate/mutilate -s $TARGET -t $DURATION -w $WARMUP \
		--keysize=fb_key --valuesize=fb_value --iadist=fb_ia \
		--update=0.033 -T 4 -c 1 $AGENTS \
		-C 1 -Q 10000
	)
}

function run_remote_seastar {
	ssh $USER@$SERVER << EOF
	sudo pkill --signal SIGINT memcached
	sleep 1
	cd /proj/uic-dcs-PG0/fshahi5/post-loom/exp/motivation/packet_drop_overhead
	sudo ./run_with_seastar.py $SERVER_CORES $SERVER_CPULIMIT
EOF
}

function terminate_remote_seastar {
	ssh $USER@$SERVER << EOF
	sudo pkill --signal SIGINT memcached
EOF
}

function usage {
	echo "Usage $0 server_cores server_cpulimit"
}


# ---------------------------
# Start from here
# ---------------------------

if [ $# -lt 2 ]; then
	usage
	exit -1
fi

SERVER_CORES="--cores $1"
SERVER_CPULIMIT="--cpulimit $2"
OUTPUT=$OUTDIR/$1_$2.txt

echo "Running Seastar"
run_remote_seastar &> $LOG &
sleep 12
echo "Running Mutilate"
tmp="`run_mutilate_master`"
echo "Stopping Seastar"
terminate_remote_seastar
sleep 2

# Gather results
SW_RES="`cat $LOG | grep -A 5 "Driver PMDPort"`"
MUTILATE_RES="`echo "$tmp" | grep -A 10 "#type"`"
echo "~~~~~~~~~~~~~~" >> $OUTPUT
echo "$MUTILATE_RES" >> $OUTPUT
echo "" >> $OUTPUT
echo "$SW_RES" >> $OUTPUT

(
	tp=`echo "$MUTILATE_RES" | grep "Total QPS" | awk '{print $4}'`
	tp=`echo "$tp / 1000" | bc`
	tmp=`echo "$MUTILATE_RES" | grep read`
	l50=`echo "$tmp" | awk '{print $7}'`
	l99=`echo "$tmp" | awk '{print $10}'`
	l999=`echo "$tmp" | awk '{print $11}'`
	l9999=`echo "$tmp" | awk '{print $12}'`
	ds=`echo "$SW_RES" | grep -A 5 "tmp_vhost0" | grep -A 1 "Out/TX" | grep "dropped" | awk '{print $2}'`
	total_pkt=`echo "$SW_RES" | grep -A 5 "tmp_vhost0" | grep -A 1 "Out/TX" | grep "packets" | awk '{print $3}'`
	echo "CPU Share	Throughput (Kqps)	RCT @50 (us)	RCT @99 (us)	RCT @99.9 (us)	RCT @99.99 (us)	Drop Server Side	Total Packets"
	printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" "=$1*$2" $tp $l50 $l99 $l999 $l9999 $ds $total_pkt
)

)
