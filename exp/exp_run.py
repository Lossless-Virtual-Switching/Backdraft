#!/usr/bin/python

import argparse
import csv
import glob
import json
import math
import os
import platform
import re
import shlex
import subprocess
import sys
import json

from time import sleep

BESS_HOME = '/proj/uic-dcs-PG0/post-loom/code/bess'
MOON_HOME = '/proj/uic-dsc-PG0/'


class VhostConf(object):
    def __init__(self, *initial_data, **kwargs):
        for dictionary in initial_data:
            for key in dictionary:
                setattr(self, key, dictionary[key])
        for key in kwargs:
            setattr(self, key, kwargs[key])


def bess(config):
    config = VhostConf(config)
    cmd = 'sudo bessctl/bessctl -- daemon start -- run file {config_file}'.format(config_file=os.path.join(config.bess_home, "bessctl/conf/port", config.bess_config), config_path="/home/alireza/Documents/post-loom/exp/config/backdraft/test_backpressure.json") # I don't know how to do this, but will do something about it.
    print(cmd)
    subprocess.check_call(cmd, cwd=config.bess_home, shell=True)


def make_sink_app(dpdk_home):
    cmd = 'make -C examples/skeleton RTE_SDK=$(pwd) RTE_TARGET=build O=$(pwd)/build/examples/skeleton'
    subprocess.check_call(cmd, cwd=dpdk_home, shell=True)


def sink_app(just_compile):
    DPDK_HOME = '/proj/uic-dcs-PG0/dpdk-stable-17.11.6/'
    make_sink_app(DPDK_HOME)
    cmd_fw_sink = 'sudo ./build/examples/skeleton/basicfwd -l 0-1 -n 4 --vdev="virtio_user0,path=/users/alireza/my_vhost0.sock,queues=1" --log-level=8 --socket-mem 1024,1024 --proc-type auto'
    if(not just_compile):
        subprocess.check_call(cmd_fw_sink, cwd=DPDK_HOME, shell=True)


def moongen_run(config):
    MOON_HOME = config['moongen_home']
    cmd = 'sudo ./build/MoonGen examples/{script_to_load} --dpdk-config={dpdk_conf} {sender_dev} {receiver_dev} {client} {server} {rqueueClient} {tqueueClient} \
        {rqueueServer} {tqueueServer} -v {clientCount} -u {clientSleepTime} -s {sleepTime} -p {dumper} -c {dumperCount} --edrop {enable_drop} -r {main_workload_rate} -t {exp_duration}'.format(sender_dev=config['sender_dev'],
        receiver_dev=config['receiver_dev'],
        main_workload_rate=config['main_workload_rate'],
        exp_duration=config['exp_duration'],
        script_to_load=config['script'],
        enable_drop=config['drop'],
        dpdk_conf=config['dpdk_config'],
        dumper=config['dumper'],
        client=config['client'],
        server=config['server'],
        tqueueClient=config['tqueueClient'],
        rqueueClient=config['rqueueClient'],
        rqueueServer=config['rqueueServer'],
        tqueueServer=config['tqueueServer'],
        dumperCount=config['dumperCount'],
        sleepTime=config['serverSleepTime'],
        clientCount=config['clientCount'],
        clientSleepTime=config['clientSleepTime']
    )

    print(cmd)
    subprocess.check_call(cmd, cwd=MOON_HOME, shell=True)
    print("Moongen exits")
    # moongen_post_exp()


def moongen_run_exp(config):
    MOON_HOME = "/proj/uic-dcs-PG0/moongen/"
    cmd = 'sudo ./build/MoonGen examples/{script_to_load} --dpdk-config={dpdk_conf} \
        {sender_dev} {receiver_dev} {client} {server} {tqueueClient} {rqueueClient} \
        {rqueueServer} {rqueueServer} --edrop {enable_drop} -r {main_workload_rate} --brate {background_workload_rate} \
        -t {exp_duration} -du {dumper}'.format(
        sender_dev=config['sender_dev'],
        receiver_dev=config['receiver_dev'],
        client=config['client'],
        server=config['server'],
        main_workload_rate=config['main_workload_rate'],
        background_workload_rate=config['background_workload_rate'],
        exp_duration=config['exp_duration'],
        script_to_load=config['script'],
        enable_drop=config['drop'],
        backpressure=config['backpressure'],
        dpdk_conf=config['dpdk_config']
    )

    print(cmd)
    subprocess.check_call(cmd, cwd=MOON_HOME, shell=True)
    print("Moongen exits")
    moongen_post_exp(config)


def moongen_post_exp(config):
    results_dir = "/proj/uic-dcs-PG0/post-loom/exp/results"
    cmd = "count=`ls -1 pings* | wc -l` && mv pings.txt pings_${count}.txt"
    subprocess.check_call(cmd, cwd=results_dir, shell=True)
    analysis(results_dir, config)


def analysis(results_dir, config):
    analysis_dir = "/proj/uic-dcs-PG0/post-loom/exp/analysis"
    cmd = "count=`ls -1  {0}/pings* | wc -l` && python3 analysis.py {0} {0}/pings_$count.txt {1} {2}".format(
        results_dir, config['background_workload_rate'], config['drop'])
    subprocess.check_call(cmd, cwd=analysis_dir, shell=True)


def analysis_manual(results_dir):
    analysis_dir = "/proj/uic-dcs-PG0/post-loom/exp/analysis"
    pings = results_dir + "/" + "pings_3.txt"
    cmd = "python3 analysis.py {0} {1}".format(results_dir, pings)
    subprocess.check_call(cmd, cwd=analysis_dir, shell=True)


def load_exp_conf(config_path):
    with open(config_path) as config_file:
        data = json.load(config_file)
    return data


def drop_latency_experiment():
    # bess_config(BESS_CONFIG_PATH) #TODO: I need to involve the config from here

    config = VhostConf(load_exp_conf("config/exp_config.json"))
    plot_data_cleanup(config.plot)

    for j in range(2):
        config.moongen['drop'] = j
        i = 0.5
        while(i < 7):
            config.moongen['background_workload_rate'] = i
            moongen_run_exp(config.moongen)
            i = i + 0.5

    draw_latency_drop_plots(config.plot)


def plot_data_cleanup(config):
    PLOT_HOME = config['plot_dir']
    drawer = config['drawer']
    cmd = "rm {0} {1} {2} {3} {4}".format(config['latency_0'],
                                          config['drop_0'], config['latency_1'],
                                          config['drop_1'], config['background_rate'])
    subprocess.call(cmd, cwd=PLOT_HOME, shell=True)


def draw_latency_drop_plots(config):
    PLOT_HOME = config['plot_dir']
    cmd = 'python3 {0} {1} {2} {3} {4} {5}'.format(config['drawer'], config['latency_0'],
                                                   config['drop_0'], config['latency_1'],
                                                   config['drop_1'], config['background_rate'])
    subprocess.check_call(cmd, cwd=PLOT_HOME, shell=True)


def redraw_latency_drop_plot():
    config = VhostConf(load_exp_conf("config/exp_config.json"))
    draw_latency_drop_plots(config.plot)

def mtcp_run(config):
    cmd = "taskset --cpu-list 16 sysbench --test=cpu --cpu-max-prime=200000000 --num-threads=%s run &" % config[
        "thread_number"]
    subprocess.call(cmd, shell=True)

def sysbench(config):
    cmd = "taskset --cpu-list 16 sysbench --test=cpu --cpu-max-prime=200000000 --num-threads=%s run &" % config[
        "thread_number"]
    subprocess.call(cmd, shell=True)


def kill_sysbench():
    cmd = "pkill sysbench"
    subprocess.check_call(cmd, shell=True)


def run(config_path):
    config = VhostConf(load_exp_conf(config_path))
    general_config = VhostConf(config.general)

    if(general_config.sysbench):
        sysbench(config.sysbench)

    if(general_config.bess):
        bess(config.bess)

    if(general_config.moongen):
        moongen_run(config.moongen)

    if(general_config.mtcp):
        mtcp_run(config.mtcp)

    if(general_config.sysbench):
        kill_sysbench()

#os.environ["RTE_SDK"] = "/proj/uic-dcs-PG0/post-loom/code/dpdk/"
#os.environ["RTE_TARGET"] = "x86_64-native-linuxapp-gcc"

parser = argparse.ArgumentParser(description='Software Switch Experiments')
parser.add_argument('--path', type=str, default='config/mtcp/test_backpressure.json', help='Absolute/relative path to the config file')
args = parser.parse_args()

#run("config/mtcp/test_backpressure.json")
run(args.path)
