#!/bin/bash

function install_mutilate {
  . /etc/os-release
  tmp=`pwd`
  if [ ! -d ./mutilate/ ]
  then
    sudo apt-get update
    sudo apt-get install -y scons libevent-dev gengetopt libzmq3-dev
    # echo "Mutilate is not setup (this scripts is not yet installing it)"
    # exit 1
    # https://oauth-key-goes-here@github.com/username/repo.git https://oauth-key-goes-here@github.com/username/repo.git
    git clone https://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@github.com/fshahinfar1/mutilate.git
    cd mutilate
    if [ "$VERSION_ID" = "20.04" ]
    then
      mv SConstruct3 SConstruct
    fi
    scons
  else
    cd ./mutilate/
    git pull https://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@github.com/fshahinfar1/mutilate.git
    scons
  fi
  cd $tmp
}

cd ~
install_mutilate

