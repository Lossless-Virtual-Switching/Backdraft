#!/usr/bin/python3

import argparse
import json
import os
import platform
import subprocess
import sys
from time import sleep

class VhostConf(object):
    def __init__(self, *initial_data, **kwargs):
        for dictionary in initial_data:
            for key in dictionary:
                setattr(self, key, dictionary[key])
        for key in kwargs:
            setattr(self, key, kwargs[key])


def bess_install(config):
    config = VhostConf(config)
    cmd = './build.py'
    print(cmd)
    subprocess.check_call(cmd, cwd=config.bess_home, shell=True)


def moongen_install(config):
    '''
    How to install moongen
    config = VhostConf(config)
    MOON_HOME = config.moongen_home
    
    subprocess.run(['mkdir', '-p', new_path])
    '''
    return new_path

def sysbench_install(config):
    '''
    How to install sysbench
    '''
    return 0

def load_exp_conf(config_path):
    with open(config_path) as config_file:
        data = json.load(config_file)
    return data

def install(config_path):
    config = VhostConf(load_exp_conf(config_path))
    general_config = VhostConf(config.general)

    #if(general_config.sysbench):
    #    sysbench(config.sysbench)

    if(general_config.bess):
        bess_install(config.bess)

    if(general_config.moongen):
        moongen_install(config.moongen, 0)

    # if(general_config.mtcp):
    #    mtcp_run(config.mtcp)

    if(general_config.sysbench):
        kill_sysbench()

    if(general_config.tas):
        tas_install(config.tas) 

    if(general_config.tas_benchmark):
        benchmark_install(config)

if __name == "__main__":
    parser = argparse.ArgumentParser(description='setting up the environment')
    parser.add_argument('--path', type=str, default='config/config.json', help='Absolute/relative path to the config file')
    args = parser.parse_args()
    install(args.path)


