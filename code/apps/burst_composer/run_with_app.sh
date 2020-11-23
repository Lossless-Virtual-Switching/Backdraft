#!/bin/bash

# stop everything running
sudo pkill burst_composer
sudo pkill udp_app

curdir=`dirname $0`
app=`realpath $curdir/../udp_client_server/build/udp_app`
prefix="m1"
port_name_1=vport_0
port_name_2=vport_1
count_queues=1
cdq="bess"

duration=10
burst_composer_delay=0  # us
server_delay=0
server_cores="6"
client_cores="(10,12)"
client_rate=1000000

burst="sudo ./build/burst_composer -l 2,4 --file-prefix=$prefix --proc-type=primary \
  --no-pci --socket-mem 15000,15000 -- $count_queues $burst_composer_delay"

server_cmd="sudo $app --no-pci -l$server_cores --file-prefix=$prefix \
  --proc-type=secondary --socket-mem=128 -- \
  bidi=false vport=$port_name_1 10.10.0.1  $count_queues $cdq server $server_delay"

client_cmd="sudo $app --no-pci --lcores=$client_cores --file-prefix=$prefix \
  --proc-type=secondary --socket-mem=128 -- \
  bidi=false vport=$port_name_2 10.10.0.2 $count_queues $cdq client 1 10.10.0.1 \
  1 $duration 5001 0 $client_rate"

echo $burst
echo
echo $server_cmd
echo
echo $client_cmd
echo

$burst&
pid=$!
sleep 10
echo
echo

$server_cmd&
sleep 2

$client_cmd&
wait $pid
