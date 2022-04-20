#!/bin/bash
# This script is executed on Mutilate master machine!

# GLOBAL VARIABLES
server_ip=node0
# NOTE: server_cores is overrided
server_cores=4

wrk_threads=16
wrk_connections=80
wrk_duration=20

remote_machines=(node2 node3 node4)
EXP_SSH_USER=$USER

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

function run_nginx_server {
  a=`nginx -v`
  if [ $? -ne 0 ]
  then
    sudo apt update
    sudo apt install -y nginx
    if [ $? -ne 0 ]
    then
      echo "Failed to install memcached on the server"
      exit 1
    fi
    # sudo systemctl stop nginx
    sudo systemctl disable nginx
    echo "Please configure nginx on server!"
    exit 1
  fi

  mark=`head -n 1 /etc/nginx/nginx.conf`
  if [ "$mark" != "#CONFIGURED" ]
  then
    echo "Nginx config does not have #CONFIGURED mark"
    exit 1
  fi
  sed_cmd="s/\(worker_processes\)\s\([1-9][0-9]*\)/\1 $server_cores/g"
  sudo sed -i "$sed_cmd" /etc/nginx/nginx.conf
  sudo systemctl start nginx.service
  sudo nginx -s reload
  echo Nginx reloaded
}

function install_wrk {
  . /etc/os-release
  tmp=`pwd`
  if [ ! -d ./wrk2/ ]
  then
    sudo apt-get update
    sudo apt-get install -y build-essential
    git clone https://github.com/giltene/wrk2
    cd ./wrk2
    make
  fi
  cd $tmp
}

function run_wrk {
  pkill wrk
  cd $HOME
  install_wrk
  cd ./wrk2/

  ./wrk -t$wrk_threads -c$wrk_connections -d${wrk_duration}s -R1000000 http://$server_ip/ &
  ./wrk -t$wrk_threads -c$wrk_connections -d${wrk_duration}s -R1000000 http://$server_ip/
}

# MAIN
check_env

echo "How many server cores?"
read server_cores

ssh $EXP_SSH_USER@$server_ip "$(set); run_nginx_server" &> /dev/null &
sleep 1

for m in ${remote_machines[@]}
do
  ssh $EXP_SSH_USER@$m "$(set); run_wrk" &> /dev/null &
done
sleep 1
run_wrk
echo "Done!"
