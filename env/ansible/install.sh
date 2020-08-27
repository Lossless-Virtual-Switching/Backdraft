#!/bin/sh

res=$(which ansible)
if [ -z "$res" ]; then
  # update cache (to install ansible)
  sudo apt-get update
  res=$(apt-cache search ^ansible\$)
  if [ ! -z "$res" ]; then 
    sudo apt-get install -yy ansible
  else 
    echo please install ansible and retry
    exit 1
  fi
  
fi

ansible-playbook -i localhost, -c local bkdrft_playbook.yml

echo "if every thing has been setuped correctly you need to reboot the system for the hugepage allocation."
echo "should the system be rebooted? (yes/no)"
read confirm
if [ "$confirm" = "yes" ]; then
  echo rebooting the system...
  sudo reboot 
fi
