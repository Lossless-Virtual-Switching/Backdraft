nohup ./boot.sh &> /dev/null &
sleep 3
nohup ./latency.sh &> /dev/null &
sleep 3
nohup ./tcp.sh &> /dev/null &
sleep 3
echo done
