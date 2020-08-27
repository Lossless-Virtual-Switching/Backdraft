#!/bin/sh

here=`realpath $(dirname $0)`;

if [ -f $here/post-loom.tar.xz ]; then
	echo archive file exists, rename or remove it before generating a new file.
	exit 1
fi

echo Copying files...
mkdir $here/post-loom/
mkdir $here/post-loom/code
mkdir $here/post-loom/exp
cp -r $here/../../../code/dpdk $here/post-loom/code/  # TODO: do not copy unwanted files here
rm -r $here/post-loom/code/dpdk/x86_64-native-linuxapp-gcc
rm -r $here/post-loom/code/dpdk/x86_64-*
echo DPDK done

cp -r $here/../../../code/tas $here/post-loom/code/
echo TAS done

cp -r $here/../../../code/tas-benchmark $here/post-loom/code/
echo TAS Benchmarks done

echo Creating .tar.xz file
tar -C $here/ -cz post-loom -f $here/post-loom.tar.xz

echo Removing files
rm -r $here/post-loom/
echo Done

