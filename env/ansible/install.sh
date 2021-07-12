#!/bin/sh

# Install python and pyyaml
sudo apt-get update
sudo apt-get install -y python3 python3-pip
sudo apt-get install -y python-pip
pip3 install -U pip
pip install -U pip
pip3 install pyyaml
pip install pyyaml

res=$(which ansible)
if [ -z "$res" ]; then
  res=$(apt-cache search ^ansible\$)
  if [ ! -z "$res" ]; then 
    sudo apt-get install -yy ansible
  else 
    echo please install ansible and retry
    exit 1
  fi
  
fi

ansible-playbook -i localhost, -c local bkdrft_playbook.yml -e 'ansible_python_interpreter=/usr/bin/python3'

echo "if every thing has been setuped correctly you need to reboot the system for the hugepage allocation."
echo "should the system be rebooted? (yes/no)"
read confirm
if [ "$confirm" = "yes" ]; then
  echo rebooting the system...
  sudo reboot 
fi
