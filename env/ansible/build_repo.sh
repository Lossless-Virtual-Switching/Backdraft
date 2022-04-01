#! /bin/bash

ansible-playbook -i localhost, -c local build_repo.yml -e 'ansible_python_interpreter=/usr/bin/python3'
