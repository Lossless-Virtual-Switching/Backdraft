#!/bin/bash
curdir=$(dirname $0)
curdir=$(realpath $curdir)
if [ ! -f "$curdir/post-loom.tar.xz" ]; then
    # TODO: consider removing the tar dependency. it is annoying.
    echo tar file does not exists, creating one!
    /bin/bash $curdir/create_postloom_tar.sh
    if [ "$?" != "0" ]; then
        echo failed creating tar file!
        exit 1
    fi
fi

# Check if shuffle has been compiled
shuffle_file=( "$curdir/shuffle/shuffle_server" "$curdir/shuffle/shuffle_client" )
for file in ${shuffle_file[@]}
do
  if [ ! -f $file ]
  then
    echo File not found: $file
    echo Compiling shuffle...
    tmp_cur=`pwd`
    cd `dirname $file`
    if [ $? -ne 0 ]
    then
      echo Shuffle directory not found
      exit -1
    fi
    make
    if [ $? -ne 0 ]
    then
      echo Make shuffle failed
      exit -1
    fi
    cd $tmp_cur
  fi
done

echo Building docker image...
sudo docker build $curdir -t tas_shuffle
