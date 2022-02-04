#!/bin/bash
# This script is executed on Mutilate master machine!

# Do not ask about the the ssh user
if [ -z $EXP_SSH_USER ]
then
  EXP_SSH_USER=$USER
fi

# GLOBAL VARIABLES
## exp_name=("Error" "ETC" "Fixed Value No BG" "Fixed Value BG")
## ETC=1
## FIXED_VALUE_NO_BG=2
## FIXED_VALUE_BG=3
## EXPERIMENT=$ETC

server_ip=node0
# server_port=11211
# NOTE: it is overriten by user input
server_cores=16

mutilate_threads=16
mutilate_connections=4
mutilate_duration=30
mutilate_warmup=0
mutilate_qps=2500000
mutilate_pipeline_depth=1

agent_machines=(node2 node3 node4)
# agent_machines=()
bg_mutilate_machine=node5
bg_mutilate_value_sz=1000000
bg_mutilate_qps=25000
bg_mutilate_active=0
if [ $bg_mutilate_machine -eq 0 ]
then
	echo "Background workload is disabled"
fi

FINISH_FILE=/tmp/_mutilate_exp_finish.txt
RESULT_DIR=/tmp/mem_exp/
RESULT_FILE=/tmp/latency.txt
RESULT_STDOUT=/tmp/mem_stdout.txt

all_hosts=($server_ip ${agent_machines[@]} $bg_mutilate_machine)
host_names=(${all_hosts[@]} localhost)

# FUNCTIONS
function check_env {
  # [ -z $MEM_GIT_USER ] || [ -z $MEM_GIT_PASS ] ||
  if [ -z $EXP_SSH_USER ]
  then
    echo $EXP_SSH_USER
    # MEM_GIT_USER, MEM_GIT_PASS and
    echo "Define EXP_SSH_USER"
    exit 1
  fi

  # if [ -d $RESULT_DIR ]
  # then
  #   echo "Last experiment's data is not moved!"
  #   exit 1
  # fi
  # mkdir -p $RESULT_DIR
}

function cores {
  count=$1
  if [ $count -lt 33 ]
  then
    cores=`seq 0 2 $((2 * count - 2)) | tr '\n' ' ' | sed 's/\s\([0-9]\)/,\1/g'`
  else
    cores=`seq 0 2 62 | tr '\n' ' ' | sed 's/\s\([0-9]\)/,\1/g'`
    cores="$cores `seq 1 2 $((2 * (count - 32) - 1)) | tr '\n' ' ' | sed 's/\s\([0-9]\)/,\1/g'`"
  fi
  echo $cores
}

function run_memcached_server {
  pkill memcached
  sleep 1
  a=`memcached -V`
  if [ $? -ne 0 ]
  then
    sudo apt update
    sudo apt install -y memcached
    if [ $? -ne 0 ]
    then
      echo "Failed to install memcached on the server"
      exit 1
    fi
    sudo systemctl stop memcached
    sudo systemctl disable memcached
  fi

  ## if [ $EXPERIMENT -eq $FIXED_VALUE_BG ]
  ## then
  ##   # Check if sysbench is installed
  ##   a=`sysbench --version`
  ##   if [ $? -ne 0 ]
  ##   then
  ##     sudo apt install -y sysbench
  ##     if [ $? -ne 0 ]
  ##     then
  ##       echo "Failed to install sysbench on the server"
  ##       exit 1
  ##     fi
  ##   fi
  ## fi

  memory=50000 # MB
  connections=1000
  cpus="`cores $server_cores`"
  # echo $cpus
  server_cmd="taskset -c $cpus memcached -l $server_ip \
    -m $memory -c $connections -t $server_cores -d"

  exec sh -c "$server_cmd" &
  # echo $server_cmd
  # if [ $EXPERIMENT -eq $FIXED_VALUE_BG ]
  # then
  #   echo "Running Background Program (sysbenc)"
  #   # --rate=10000
  #   taskset -c 1-$server_cores sysbench --threads=$server_cores \
  #     --time=$((mutilate_duration + mutilate_warmup + 10)) --forced-shutdown=2 cpu run
  # fi
}

function install_mutilate {
  . /etc/os-release
  tmp=`pwd`
  if [ ! -d ./mutilate/ ]
  then
    sudo apt-get update
    sudo apt-get install -y scons libevent-dev gengetopt libzmq3-dev
    # echo "Mutilate is not setup (this scripts is not yet installing it)"
    # exit 1
    # https://oauth-key-goes-here@github.com/username/repo.git https://oauth-key-goes-here@github.com/username/repo.git
    git clone https://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@github.com/fshahinfar1/mutilate.git
    cd mutilate
    if [ "$VERSION_ID" = "20.04" ]
    then
      mv SConstruct3 SConstruct
    fi
    scons
  else
    cd ./mutilate/
    git pull https://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@github.com/fshahinfar1/mutilate.git
    scons
  fi
  cd $tmp
}

function run_agent {
  pkill mutilate
  if [ -e $FINISH_FILE ]
  then
    rm $FINISH_FILE
  fi
  cd $HOME
  install_mutilate &> /dev/null
  sleep 1
  cd mutilate
  taskset -c "$(cores $mutilate_threads)" ./mutilate -A \
    -T $mutilate_threads &> $RESULT_STDOUT &

  # TODO: This is not elegant but ...
  sleep $((mutilate_duration + 5))
  echo "done " > $FINISH_FILE
}

function run_master {
  # pkill mutilate
  cd $HOME
  # install_mutilate
  cd mutilate
  agents=""
  for a in ${agent_machines[@]}
  do
    agents="$agents -a $a "
  done
  echo "Agents: $agents"
  # key_value_data="--keysize=fb_key --valuesize=fb_value --iadist=fb_ia --update=0.033"
  key_value_data="--keysize=fb_key --valuesize=pareto:0,2144,0.348238 --iadist=fb_ia --update=0.033"

  # case $EXPERIMENT in
  #   $ETC)
  #     key_value_data="--keysize=fb_key --valuesize=pareto:0,2144,0.348238 --iadist=fb_ia --update=0.033"
  #     ;;
# $FIXED_VALUE_NO_BG)
  #     # TODO: It is duplicate of FIXED_VALUE_BG
  #     key_value_data="--keysize=20 --valuesize=4800"
  #     ;;
# $FIXED_VALUE_BG)
  #     # TODO: It is duplicate of FIXED_VALUE_NO_BG
  #     key_value_data="--keysize=20 --valuesize=4800"
  #     ;;
  # esac

  cmd="taskset -c $(cores $mutilate_threads) ./mutilate $agents --noload \
    -T $mutilate_threads -c $mutilate_connections \
    -s $server_ip -t $mutilate_duration $key_value_data \
    -w $mutilate_warmup -d $mutilate_pipeline_depth \
    -q $mutilate_qps"
    # -Q 20000 -C 1 -D 1" # --save=$RESULT_FILE --agent-sampling

  echo $cmd
  eval $cmd

}

function run_load_data {
  key_value_data="--keysize=fb_key --valuesize=fb_value --iadist=fb_ia --update=0.033"
  cd $HOME
  install_mutilate &> /dev/null
  cd mutilate
  ./mutilate --loadonly -s $server_ip $key_value_data
  ./mutilate --loadonly -s $server_ip \
    --keysize=111 --valuesize=$bg_mutilate_value_sz

  echo "Data loaded"
}

function run_bg_mutilate {
  if [ -e $FINISH_FILE ]
  then
    rm $FINISH_FILE
  fi
  cd $HOME
  install_mutilate &> /dev/null
  cd mutilate
  ./mutilate --noload -T 4 -c 2 -s $server_ip -t $((mutilate_duration + 2)) \
    --keysize=111 --valuesize=$bg_mutilate_value_sz \
    -q $bg_mutilate_qps &> $RESULT_STDOUT # --records=y --iadist=exponential:1.5

  echo "done " > $FINISH_FILE
}

function get_host_retransmitted_segs {
  host=$1
  cmd='netstat -s | grep "segments retransmitted"'
  result=$(ssh $EXP_SSH_USER@$host "$cmd")
  result=$(echo $result | cut -d " " -f 1)
  echo $result
}

function get_all_retrans {
  results=()
  for m in ${all_hosts[@]}
  do
    tmp=$(get_host_retransmitted_segs $m)
    results+=($tmp)
  done

  tmp=`netstat -s | grep "segments retransmitted"`
  tmp=`echo $tmp | cut -d " " -f 1`
  results+=($tmp)
  echo ${results[@]}
}

function run_remote_cmd {
  host=$1
  cmd=$2
  ssh $EXP_SSH_USER@$host "$cmd" # &> /dev/null
  ret=$?
  if [ $ret -ne 0 ]
  then
    printf "Running cmd: \n\t`$cmd | egrep -oh '; .*$'`\non machine $host failed!\n"
    echo "Return code: $ret"
    exit 1
  fi
}

# MAIN
check_env
# echo "Experiment is ${exp_name[$EXPERIMENT]}"

echo "What is background value size? "
read bg_mutilate_value_sz

server_setup=1
if [ "x$1" = "x--no-server-setup" ]
then
  server_setup=0
fi

if [ $server_setup -gt 0 ]; then
  echo "What is number of server cores? "
  read server_cores
  if [ -z $server_cores ] || [ "$server_cores" -le 0 ] || [ "$server_cores" -gt 32 ]
  then
    echo "Number of server cores are out of acceptable region"
    exit 1
  fi
  echo Value size: $bg_mutilate_value_sz
  echo Server cores: $server_cores
  echo "Setting up server machine..."
  # ssh $EXP_SSH_USER@$server_ip "$(set); run_memcached_server" # &> /dev/null &
  run_remote_cmd $server_ip "$(set); run_memcached_server"
  sleep 1
fi

for m in ${agent_machines[@]}
do
  # ssh $EXP_SSH_USER@$m "$(set); run_agent" &> /dev/null &
  run_remote_cmd $m "$(set); run_agent" &
done
sleep 1

run_load_data
before_seg_retr=($(get_all_retrans))
if [ $bg_mutilate_active -gt 0 ]
then
  # ssh $EXP_SSH_USER@$bg_mutilate_machine "$(set); run_bg_mutilate" &> /dev/null &
  run_remote_cmd $bg_mutilate_machine "$(set); run_bg_mutilate" &
fi
run_master
echo "Master finished"

# TODO: not good to add ...
machines=(${agent_machines[@]})
machines+=($bg_mutilate_machine)
for m in ${machines[@]}
do
  ssh $EXP_SSH_USER@$m /bin/bash <<EOF
    while [ ! -e $FINISH_FILE ]
    do
      sleep 1
    done
    echo "$m is finished."
    pkill mutilate
EOF
done

# Kill server
# ssh $EXP_SSH_USER@$server_ip "pkill memcached"
run_remote_cmd $server_ip "pkill memcached"

# Calcuate segment retransmitions
after_seg_retr=($(get_all_retrans))
diff_seg_retr=()

printf "\nNumber segment retransmision:\n"
i=0
for b in ${before_seg_retr[@]}
do
  tmp=$((after_seg_retr[$i] - before_seg_retr[$i]))
  diff_seg_retr+=($tmp)
  printf "\t${host_names[$i]}: $tmp"
  i=$((i+1))
done
printf "\n"
# ---------------------------------------

# cd $RESULT_DIR
# mv $RESULT_FILE ./0.txt
# k=1
# for m in ${agent_machines[@]}
# do
#   scp $EXP_SSH_USER@$m:$RESULT_FILE ./$k.txt
#   k=$((k+1))
# done

echo "Done!"
