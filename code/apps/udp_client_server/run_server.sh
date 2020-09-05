echo need sudo
sudo echo access granted

pci="7:00.1"
if [ ! -z "$1" ]; then
	type=$1
else
	type=bess
fi
delay=40
count_queue=8
source_ip="192.168.1.3"

echo ================
echo "type: $type   "
echo ================

# run server
sudo ./build/udp_app \
	-l 4 \
	-w $pci \
	--file-prefix=m1 \
	-- $source_ip $count_queue $type server $delay
