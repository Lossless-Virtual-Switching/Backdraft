#!/bin/bash
# Nginx server has been setuped to server NSDI21 website
if [ -z $EXP_SSH_USER ]
then
  echo "Please set EXP_SSH_USER before running this script!"
  exit 1
fi
# MACHINES to ssh and run experiment on
MACHINES=(node2 node3 node4)

echo "These machines will cooperate in experiment"
echo "    ${MACHINES[@]}"
echo "!! warning: make sure clock are sync between machines"
echo "!! warning: make sure you have added other mahcnies to trusted hosts"
echo

server_ip=node0
server_port=80
links=()
links+=(http://$server_ip:$server_port/conference/nsdi21.html)
links+=(http://$server_ip:$server_port/conference/nsdi21/call-for-papers.html)
links+=(http://$server_ip:$server_port/conference/nsdi21/technical-sessions.html)
links+=(http://$server_ip:$server_port/conference/nsdi21/instructions-presenters.html)
links+=(http://$server_ip:$server_port/conference/nsdi21/presentation/ferguson.html)
links+=(http://$server_ip:$server_port/conference/nsdi21/files/nsdi21-liu-guyue.pdf)

echo "Target URLs are:"
echo
for link in ${links[@]}
do
  echo "    $link"
done
echo


NGINX_EXPERIMENT_FIN_FILE=/tmp/_nginx_exp_finished
# This is function has the main role of running the experiment
function run_exp {
  # This file will be created when experiment is done
  if [ -e $NGINX_EXPERIMENT_FIN_FILE ]
  then
    rm $NGINX_EXPERIMENT_FIN_FILE
  fi
  # if ab is install global then uncomment this
  # ab=ab
  # else set the path
  # ab=/users/$EXP_SSH_USER/httpd-ab/support/ab
  ab=/proj/uic-dcs-PG0/$EXP_SSH_USER/httpd-ab/support/ab
  req=250000
  concurrent=1
  k=0
  echo "Trace files are here:"
  for link in ${links[@]}
  do
    k=$((k+1)) 
    filename=`basename $link`
    filename=${filename%.*}
    filename="${k}_${filename}"
    trace_file="/tmp/$filename.txt"
    echo "    $trace_file"
    $ab -k -n $req -c $concurrent -a $trace_file $link > /tmp/stdout_$filename.txt &> /dev/null &
  done
  echo

  echo "EXPERIMENT FINISHED" > $NGINX_EXPERIMENT_FIN_FILE
  wait
}


# Ask other machines to start experiment
for m in ${MACHINES[@]}
do
  ssh $EXP_SSH_USER@$m "$(set); run_exp" &> /dev/null &
done


# Start experiment on local machine
run_exp
wait
echo Local machine is finished 


# Wait until other machines are done
for m in ${MACHINES[@]}
do
  ssh $EXP_SSH_USER@$m /bin/bash <<EOF
    while [ ! -e $NGINX_EXPERIMENT_FIN_FILE ]
    do
      sleep 1
    done
    echo "$m is finished."
EOF
&> /dev/null
done


# Gather files
trace_files=()
k=0
for link in ${links[@]}
do
  k=$((k+1)) 
  filename=`basename $link`
  filename=${filename%.*}
  filename="${k}_${filename}"
  trace_file="/tmp/$filename.txt"
  trace_files+=($trace_file)
done

aggregate_dir=/tmp/nginx_exp_multinode
mkdir -p $aggregate_dir
k=0
for m in ${MACHINES[@]}
do
  for f in ${trace_files[@]}
  do
    k=$((k+1))
    scp $EXP_SSH_USER@$m:$f $aggregate_dir/$k.txt &> /dev/null
    if [ $? -ne 0 ];
    then
      echo "Error: failed to get $f from $m"
    fi
  done
done
echo "All trace files should have been gathered at:"
echo "        $aggregate_dir"
echo Done
