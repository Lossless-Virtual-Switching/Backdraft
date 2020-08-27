curdir=$(dirname $0)
export RTE_SDK=$(realpath $curdir/../../../code/dpdk)
echo $RTE_SKD

cur=`pwd`

if [ ! -d $RTE_SDK/x86_64-native-linuxapp-gcc ]; then
	echo "Building DPDK ..."
	cd $RTE_SDK
	make config T=x86_64-native-linuxapp-gcc
	make -j 10 install T=x86_64-native-linuxapp-gcc
fi

cd $cur

if [ -d build/ ]; then
	rm -r build/
fi

# protobuf-c setup
# protoc-c --c_out=./utils/include/ protobuf/bkdrft_msg.proto

echo "Start building applications ... " $RTE_SDK

make
