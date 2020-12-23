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
sudo docker build $curdir -t tas_memcached_shuffle
