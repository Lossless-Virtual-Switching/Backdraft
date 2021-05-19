#!/bin/bash
# Nginx server has been setuped to server NSDI21 website
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


# if ab is install global then uncomment this
# ab=ab
# else set the path
ab=/users/fshahi5/httpd-ab/support/ab
req=1000000
timeout=30
concurrent=1
echo "Trace files are here:"
for link in ${links[@]}
do
  filename=`basename $link`
  filename=${filename%.*}
  trace_file="/tmp/$filename.txt"
  echo "    $trace_file"
  # timeout ${timeout}s
  $ab -k -n $req -c $concurrent -a $trace_file $link > /tmp/stdout_$filename.txt &
done
echo

wait
echo "Done"
