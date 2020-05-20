curdir=$(dirname $0)
export RTE_SDK=$(realpath $curdir/../../dpdk)
rm -r build/
make
