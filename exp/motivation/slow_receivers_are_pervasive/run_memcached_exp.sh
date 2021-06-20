#!/bin/bash
# This script is executed on Mutilate master machine!

# GLOBAL VARIABLES
server_ip=node0
# server_port=11211
server_cores=4

mutilate_threads=16
mutilate_connections=4
mutilate_duration=20

# mutilate_valuesize=1000000
mutilate_valuesize=4800
mutilate_keysize=20
mutilate_records=100

agent_machines=(node2 node3 node4)

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
}

function run_memcached_server {
  pkill memcached
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

  memory=50000 # MB
  connections=2000
  server_cmd="taskset -c 1-$server_cores memcached -l $server_ip -m $memory -c $connections -t $server_cores -d"
  exec sh -c "$server_cmd"
}

function install_mutilate {
  . /etc/os-release
  tmp=`pwd`
  if [ ! -d ./mutilate/ ]
  then
    sudo apt-get update
    sudo apt-get install -y scons libevent-dev gengetopt libzmq3-dev
    echo "Mutilate is not setup (this scripts is not yet installing it)"
    exit 1
    # git clone https://$MEM_GIT_USER:$MEM_GIT_PASS@github.com/fshahinfar1/mutilate.git
    # cd mutilate
    # if [ "$VERSION_ID" -eq "20.04" ]
    # then
    #   mv SConstruct3 SConstruct
    # fi
    # scons
  fi
  cd $tmp
}

function run_agent {
  pkill mutilate
  cd $HOME
  install_mutilate
  cd mutilate
  nohup ./mutilate -A -T $mutilate_threads &> /dev/null &
}

function run_master {
  pkill mutilate
  cd $HOME
  install_mutilate
  cd mutilate
  agents=""
  for a in ${agent_machines[@]}
  do
    agents="$agents -a $a "
  done
  echo "Agents: $agents"
  key_value_data="--keysize=$mutilate_keysize --valuesize=$mutilate_valuesize -r $mutilate_records"

  if [ $server_setup -gt 0 ]; then
    ./mutilate --loadonly -s $server_ip $key_value_data
  fi
  echo "Data loaded"
  ./mutilate $agents -T $mutilate_threads -c $mutilate_connections -s $server_ip -t $mutilate_duration $key_value_data
}

# MAIN
check_env

echo "Value size is $mutilate_valuesize bytes"

server_setup=1
if [ "x$1" = "x--no-server-setup" ]
then
  server_setup=0
fi

if [ $server_setup -gt 0 ]; then
  echo "How many server cores?"
  read server_cores

  ssh $EXP_SSH_USER@$server_ip "$(set); run_memcached_server" &> /dev/null &
  sleep 1
fi

for m in ${agent_machines[@]}
do
  ssh $EXP_SSH_USER@$m "$(set); run_agent" &> /dev/null &
done
sleep 1
run_master
echo "Done!"
