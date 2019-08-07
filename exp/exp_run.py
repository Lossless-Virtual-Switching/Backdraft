#!/usr/bin/python

import argparse
import csv
import glob
import json
import math
import numpy
import os
import paramiko
import platform
import re
import scipy
import scipy.stats
import scipy.interpolate
import shlex
import subprocess
import sys
import yaml

from time import sleep

'''
TESTPROG_CONFIG_DEFAULTS = {
    'prog': TEST_PROG_IPERF,
    'name': 'iperf_1',
    'ip': '10.10.101.2',
    'port': 5001,
    'start': 0,
    'finish': 10,
    'num_conns': 1,
    'sink': False,
    'udp': False,
    'bw': '0',
}
'''

CONTAINER_CONFIG_DEFAULTS = {
    'name': 'o2o_def',
    'iface': 'iface_o2o_def',
    'ip': '10.10.101.1',
    'cpus': 1,
    'apps': [],
}

ONE2ONE_CONFIG_DEFAULTS = {
    'containers': [CONTAINER_CONFIG_DEFAULTS],
    'bessctl': 'one_to_one_docker.bess',
    'expname': 'one_to_one',
    'backpressure': False,
    'extra_name': '',
    'run': 1,
}

DEFAULT_SRC_CONFIG = {
        'bess': True,
        'net': 'ploom',
        'ip': '10.10.101.2',
        'cpus': 1,
        'name': 'src'
        }

DEFAULT_DEST_CONFIG = {
        'bess': True,
        'net': 'ploom',
        'ip': '10.10.101.3',
        'cpus': 1,
        'name': 'dest'
        }

CLIENT_EXP_CONFIG = {
        'ip': '10.10.101.3',
        'name': 'src',
        'port': 5001,
        'time': 120
        }

SERVER_EXP_CONFIG = {
        'name': 'dest',
        'port': 5001,
        'time': 130 
        }

BESS_CONFIG_PATH = '/proj/uic-dcs-PG0/post_loom/one_to_one_docker.bess'

BESS_HOME = '/proj/uic-dcs-PG0/bess'

MOON_HOME = '/proj/uic-dsc-PG0/'

class VhostConf(object):
    def __init__(self, *initial_data, **kwargs):
        for dictionary in initial_data:
            for key in dictionary:
                setattr(self, key, dictionary[key])
        for key in kwargs:
            setattr(self, key, kwargs[key])



def docker_network_setup(network_name):
    print("Network Setup")
    cmd = 'docker network rm %s' %network_name
    subprocess.call(cmd, shell=True)
    cmd = 'docker network create --subnet=10.10.101.0/24 %s' %network_name
    subprocess.check_call(cmd, shell=True)

    


def docker_run(config):
    if(config['bess']):
        cmd = 'docker run --privileged -i -d -t --rm --name={name} ' \
                '--net={net} --cpus={cpus} ' \
                '-v /proj/uic-dcs-PG0/post_loom/output_dir:/output_dir testimage /bin/bash'

        cmd = cmd.format(name=config['name'],
            net=config['net'],
            #ip=config['ip'],
            cpus=config['cpus']
            )
        
    else:
        cmd = 'docker run --privileged -i -d -t --rm --name={name} ' \
                '--net={net} --ip={ip} --cpus={cpus} ' \
                '-v /proj/uic-dcs-PG0/post_loom/output_dir:/output_dir testimage /bin/bash'

        cmd = cmd.format(name=config['name'],
            net=config['net'],
            ip=config['ip'],
            cpus=config['cpus']
            )


    docker_id = subprocess.check_call(cmd, shell=True)
    print(cmd)

def docker_stop_all():
    cmd = 'docker stop %s'
    subprocess.call(cmd %'src', shell=True)
    subprocess.call(cmd %'dest', shell=True)
    print(cmd %'dest', cmd %'src')

def setup_testbed():
    docker_stop_all()
    docker_network_setup(DEFAULT_SRC_CONFIG['net'])
    docker_run(DEFAULT_SRC_CONFIG)
    docker_run(DEFAULT_DEST_CONFIG)

def get_iperf_exp_client_command(sink_config):
    exp_command = 'iperf3 -c {ip} -p {port} -t {time} -Z -J -u -b 0 -P 1 > /output_dir/client.txt'
    exp_command = exp_command.format(ip = sink_config['ip'],
            port=sink_config['port'],
            time=sink_config['time'])

    ##TODO adding udp config

    cmd = 'docker exec -d {name} bash -c "{cmd}"'.format(
            name= sink_config['name'],
            cmd=exp_command
            )
    print(cmd)
    return cmd

def get_iperf_exp_server_command(server_config):
    exp_command = 'iperf3 -s -p {port} -J > /output_dir/server.txt'
    exp_command = exp_command.format(port=server_config['port'])
    cmd = 'docker exec -d {name} bash -c "{cmd}"'.format(
            name= server_config['name'],
            cmd=exp_command
            )
    print(cmd)
    return cmd

def finish_iperf_experiment(server_config):
    cmd = 'docker exec -d {name} bash -c "pkill iperf3"'.format(
            name=server_config['name'])
    sleep(CLIENT_EXP_CONFIG['time'])
    subprocess.check_call(cmd, shell=True)

def run_iperf_experiment():
    cmd = get_iperf_exp_server_command(SERVER_EXP_CONFIG)
    subprocess.check_call(cmd, shell=True)
    cmd = get_iperf_exp_client_command(CLIENT_EXP_CONFIG)
    subprocess.check_call(cmd, shell=True)
    finish_iperf_experiment(SERVER_EXP_CONFIG)

def bess_config(config_path):
    config_path = "/proj/uic-dcs-PG0/post-loom/code/bess/bessctl/conf/port/alireza.bess"
    cmd = 'sudo bessctl/bessctl -- daemon start -- run file %s' %config_path
    print cmd
    bessctl_proc = subprocess.check_call(cmd, cwd=BESS_HOME, shell=True)

def make_sink_app(dpdk_home):
    cmd = 'make -C examples/skeleton RTE_SDK=$(pwd) RTE_TARGET=build O=$(pwd)/build/examples/skeleton'
    make_proc = subprocess.check_call(cmd, cwd=dpdk_home, shell=True)

def sink_app(just_compile):
    DPDK_HOME='/proj/uic-dcs-PG0/dpdk-stable-17.11.6/'
    make_sink_app(DPDK_HOME)
    cmd_fw_sink = 'sudo ./build/examples/skeleton/basicfwd -l 0-1 -n 4 --vdev="virtio_user0,path=/users/alireza/my_vhost0.sock,queues=1" --log-level=8 --socket-mem 1024,1024 --proc-type auto'
    if(not just_compile):
        subprocess.check_call(cmd_fw_sink, cwd=DPDK_HOME, shell=True)
    #subprocess.Popen([cmd_fw_sink], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

def moongen_run():
    MOON_HOME="/proj/uic-dcs-PG0/MoonGen/"
    cmd = 'sudo ./moongen-simple start load-latency:"virtio_user0,path=/tmp/my_vhost1.sock,queues=1":"virtio_user0,path=/tmp/my_vhost1.sock,queues=1":rate=10Mp/s,time=3m'
    print cmd
    moon_gen_proc = subprocess.check_call(cmd, cwd=MOON_HOME, shell=True)

def packet_gen():
    PACKET_GEN_HOME='/proj/uic-dcs-PG0/pktgen-dpdk'
    cmd_PG = 'sudo ./app/x86_64-native-linuxapp-gcc/app/pktgen -l 0,1 -n 4 --vdev="virtio_user0,path=/users/alireza/my_vhost1.sock,queues=1" --log-level=8 --socket-mem 1024,1024 --proc-type auto -b 81:00.1 -b 81:00.0 -- -T -P -m [1].0 -s 0:pcap/large.pcap -f themes/black-yellow.theme'
    #cmd_PG = './tools/run.py default'
    subprocess.check_call(cmd_PG, cwd=PACKET_GEN_HOME, shell=True)


os.environ["RTE_SDK"] = "/proj/uic-dcs-PG0/dpdk-stable-18.11.2/"
os.environ["RTE_TARGET"] = "x86_64-native-linuxapp-gcc"

#docker_stop_all()
#setup_testbed()
bess_config(BESS_CONFIG_PATH)
#moongen_run()
#sink_app(True)
packet_gen()
#run_iperf_experiment()
