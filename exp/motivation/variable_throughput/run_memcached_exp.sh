#!/bin/bash
# This script is executed on Mutilate master machine!

# GLOBAL VARIABLES
exp_name=("Error" "ETC" "Fixed Value No BG" "Fixed Value BG")
ETC=1
FIXED_VALUE_NO_BG=2
FIXED_VALUE_BG=3
EXPERIMENT=$ETC

server_ip=node0
# server_port=11211
server_cores=32

mutilate_threads=16
mutilate_connections=4
mutilate_duration=7
mutilate_warmup=5

agent_machines=(node2 node3 node4)

FINISH_FILE=/tmp/_mutilate_exp_finish.txt
RESULT_DIR=/tmp/mem_exp/
RESULT_FILE=/tmp/latency.txt

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

  if [ -d $RESULT_DIR ]
  then
    echo "Last experiment's data is not moved!"
    exit 1
  fi
  mkdir -p $RESULT_DIR
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

  if [ $EXPERIMENT -eq $FIXED_VALUE_BG ]
  then
    # Check if sysbench is installed
    a=`sysbench --version`
    if [ $? -ne 0 ]
    then
      sudo apt install -y sysbench
      if [ $? -ne 0 ]
      then
        echo "Failed to install sysbench on the server"
        exit 1
      fi
    fi
  fi

  memory=50000 # MB
  connections=2048
  server_cmd="taskset -c 1-$server_cores memcached -l $server_ip -m $memory -c $connections -t $server_cores -d"
  exec sh -c "$server_cmd" &
  echo $server_cmd
  if [ $EXPERIMENT -eq $FIXED_VALUE_BG ]
  then
    echo "Running Background Program (sysbenc)"
    # --rate=10000
    taskset -c 1-$server_cores sysbench --threads=$server_cores \
      --time=$((mutilate_duration + mutilate_warmup + 10)) --forced-shutdown=2 cpu run
  fi
}

function install_mutilate {
  . /etc/os-release
  tmp=`pwd`
  if [ ! -d ./mutilate/ ]
  then
    sudo apt-get update
    sudo apt-get install -y scons libevent-dev gengetopt libzmq3-dev
    git clone https://github.com/Lossless-Virtual-Switching/mutilate
  fi
  cd mutilate
  if [ ! -f ./mutilate ]
  then
    # build the repo
    if [ "$VERSION_ID" = "20.04" ] && [ -f SConstruct3 ]
    then
      mv SConstruct3 SConstruct
    else
      echo "if your system does not have python2 then rename SConstruct3 as SConstruct"
    fi
    scons
  fi
  cd $tmp
}

function run_agent {
  pkill mutilate
  sleep 1
  if [ -e $FINISH_FILE ]
  then
    rm $FINISH_FILE
  fi
  cd $HOME
  install_mutilate
  cd mutilate
  nohup ./mutilate -A -T $mutilate_threads &> /dev/null &
  # TODO: This is not elegant but ...
  sleep $((mutilate_duration + 5))
  echo "done " > $FINISH_FILE
}

function run_master {
  # pkill mutilate
  cd $HOME
  install_mutilate
  cd mutilate
  agents=""
  for a in ${agent_machines[@]}
  do
    agents="$agents -a $a "
  done
  echo "Agents: $agents"
  # key_value_data="--keysize=fb_key --valuesize=fb_value --iadist=fb_ia --update=0.033"
  case $EXPERIMENT in
    $ETC)
      key_value_data="--keysize=fb_key --valuesize=pareto:0,2144,0.348238 --iadist=fb_ia --update=0.033"
      ;;
    $FIXED_VALUE_NO_BG)
      # TODO: It is duplicate of FIXED_VALUE_BG
      key_value_data="--keysize=20 --valuesize=4800"
      ;;
    $FIXED_VALUE_BG)
      # TODO: It is duplicate of FIXED_VALUE_NO_BG
      key_value_data="--keysize=20 --valuesize=4800"
      ;;
  esac

  if [ $server_setup -gt 0 ]; then
    ./mutilate --loadonly -s $server_ip $key_value_data
  fi
  echo "Data loaded"
  ./mutilate $agents -T $mutilate_threads -c $mutilate_connections \
    -s $server_ip -t $mutilate_duration $key_value_data --agent-sampling \
    --save=$RESULT_FILE -w $mutilate_warmup
  }

# MAIN
check_env
echo "Experiment is ${exp_name[$EXPERIMENT]}"

server_setup=1
if [ "x$1" = "x--no-server-setup" ]
then
  server_setup=0
fi

if [ $server_setup -gt 0 ]; then
  # echo "How many server cores?"
  # read server_cores
  echo "Setting up server machine..."
  ssh $EXP_SSH_USER@$server_ip "$(set); run_memcached_server" &> /dev/null &
  sleep 1
fi

for m in ${agent_machines[@]}
do
  ssh $EXP_SSH_USER@$m "$(set); run_agent" &> /dev/null &
done
sleep 1
run_master
echo "Master finished"

for m in ${agent_machines[@]}
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

cd $RESULT_DIR
mv $RESULT_FILE ./0.txt
k=1
for m in ${agent_machines[@]}
do
  scp $EXP_SSH_USER@$m:$RESULT_FILE ./$k.txt
  k=$((k+1))
done

echo "Done!"
