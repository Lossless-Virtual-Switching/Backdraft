#/bin/bash
(
SERVER=amd012.utah.cloudlab.us
USER=$USER
LOG=/tmp/_tas_exp_log.txt
OUTDIR=/tmp/out/

if [ ! -d $OUTDIR ]; then
	mkdir $OUTDIR
fi

function run_local {
	(sudo ./run.py --client --cores 8 --duration 30)
}


function run_remote {
	ssh $USER@$SERVER << EOF
	sudo pkill --signal SIGINT run.py
	sleep 1
	cd /proj/uic-dcs-PG0/fshahi5/post-loom/exp/motivation/packet_drop_overhead
	sudo ./run.py $SERVER_CORES $SERVER_CPULIMIT
EOF
}

function terminate_remote_seastar {
	ssh $USER@$SERVER << EOF
	sudo pkill --signal SIGINT run.py
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

echo "Running Server"
run_remote &> $LOG &
sleep 12
echo "Running Client"
tmp="`run_local`"
echo "Stopping Server"
terminate_remote_seastar
sleep 2

# Gather results
REMOTE_SW_RES="`cat $LOG | grep -A 5 "Driver PMDPort"`"
LOCAL_SW_RES="`echo "$tmp" | grep -A 5 "Driver PMDPort"`"
LAST_STATS="`echo "$tmp" | grep stats | tail -n 1`"
CLIENT_RES="`echo "$tmp" | grep "flow="`"
echo "~~~~~~~~~~~~~~" >> $OUTPUT
echo "Remote Bess" >> $OUTPUT
echo "$REMOTE_SW_RES" >> $OUTPUT
echo "Local Bess" >> $OUTPUT
echo "$LOCAL_SW_RES" >> $OUTPUT
echo "" >> $OUTPUT
echo "$LAST_STATS" >> $OUTPUT
echo "$CLIENT_RES" >> $OUTPUT

# (
# 	tp=`echo "$MUTILATE_RES" | grep "Total QPS" | awk '{print $4}'`
# 	tp=`echo "$tp / 1000" | bc`
# 	tmp=`echo "$MUTILATE_RES" | grep read`
# 	l50=`echo "$tmp" | awk '{print $7}'`
# 	l99=`echo "$tmp" | awk '{print $10}'`
# 	l999=`echo "$tmp" | awk '{print $11}'`
# 	l9999=`echo "$tmp" | awk '{print $12}'`
# 	ds=`echo "$SW_RES" | grep -A 5 "tmp_vhost0" | grep -A 1 "Out/TX" | grep "dropped" | awk '{print $2}'`
# 	total_pkt=`echo "$SW_RES" | grep -A 5 "tmp_vhost0" | grep -A 1 "Out/TX" | grep "packets" | awk '{print $3}'`
# 	echo "CPU Share	Throughput (Kqps)	RCT @50 (us)	RCT @99 (us)	RCT @99.9 (us)	RCT @99.99 (us)	Drop Server Side	Total Packets"
# 	printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" "=$1*$2" $tp $l50 $l99 $l999 $l9999 $ds $total_pkt
# )

)
