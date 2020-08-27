#!/bin/sh
sudo apt-get install -y software-properties-common
sudo apt-add-repository -y ppa:ansible/ansible
sudo apt-get update
sudo apt-get install -y ansible
ansible-playbook -i localhost, -c local env/build-dep.yml
ansible-playbook -i localhost, -c local env/runtime.yml  # if you want to run BESS on the same machine.

