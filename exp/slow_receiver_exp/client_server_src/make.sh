curdir=$(dirname $0)
export RTE_SDK=$(realpath $curdir/../../../code/dpdk)
if [ -d build/ ]; then
	rm -r build/
fi
make
