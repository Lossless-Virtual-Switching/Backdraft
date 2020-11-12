curdir=$(dirname $0)
export RTE_SDK=$(realpath $curdir/../../../code/bess-v1/deps/dpdk-19.11.1/)
export RTE_TARGET=build
echo $RTE_SKD

echo "Start building applications ... "

make clean
make
