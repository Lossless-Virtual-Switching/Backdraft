#!/usr/bin/python3

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
import signal 
from time import sleep
from subprocess import PIPE

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
    cmd = 'sudo bessctl/bessctl -- daemon start -- run file {config_file}'.format(config_file=config.bess_config) # I don't know how to do this, but will do something about it.
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

def moongen_server_command_maker(config, cpu_limit):
    popen_server_cmd = './build/MoonGen examples/{script_to_load} --dpdk-config={dpdk_conf} {sender_dev} {receiver_dev} {client} {server} {rqueueClient} {tqueueClient} {rqueueServer} {tqueueServer} {result_path} -v {clientCount} -u {clientSleepTime} -s {sleepTime} -c {serverCount} -k {dumper} --edrop {enable_drop} -r {main_workload_rate} -t {exp_duration} -b {conn} -l {requests} -a {cpu}' \
    .format(sender_dev=config.server['dev'],
            receiver_dev=config.client['dev'],
            main_workload_rate=config.client['rate'],
            exp_duration=config.exp_duration,
            script_to_load=config.script,
            enable_drop=config.client['drop'],
            dpdk_conf=config.server["dpdk_config"],
            client=0,
            server=1,
            tqueueClient=config.client['tqueue'],
            rqueueClient=config.client['rqueue'],
            rqueueServer=config.server['rqueue'],
            tqueueServer=config.server['tqueue'],
            serverCount=config.server['count'],
            dumper=config.server['dumper'],
            sleepTime=config.server['SleepTime'],
            clientCount=config.client['count'],
            clientSleepTime=config.client['SleepTime'],            
            conn=config.client['concurrency'],
            requests=config.server['requests'],
            cpu=cpu_limit,
            result_path=config.results_dir)
    print("server cmd", popen_server_cmd)
    return popen_server_cmd

def moongen_client_command_maker(config, cpu_limit):
    client_cmd = 'sudo ./build/MoonGen examples/{script_to_load} --dpdk-config={dpdk_conf} {sender_dev} {receiver_dev} {client} {server} {rqueueClient} {tqueueClient} {rqueueServer} {tqueueServer} {result_path} -v {clientCount} -u {clientSleepTime} -s {sleepTime} -c {serverCount} -k {dumper} --edrop {enable_drop} -r {main_workload_rate} -t {exp_duration} -b {conn} -l {requests} -a {cpu}' \
    .format(sender_dev=config.client['dev'],
            receiver_dev=config.server['dev'],
            main_workload_rate=config.client['rate'],
            exp_duration=config.exp_duration,
            script_to_load=config.script,
            enable_drop=config.client['drop'],
            dpdk_conf=config.client["dpdk_config"],
            client=1,
            server=0,
            tqueueClient=config.client['tqueue'],
            rqueueClient=config.client['rqueue'],
            rqueueServer=config.server['rqueue'],
            tqueueServer=config.server['tqueue'],
            serverCount=config.server['count'],
            dumper=config.server['dumper'],
            sleepTime=config.server['SleepTime'],
            clientCount=config.client['count'],
            clientSleepTime=config.client['SleepTime'],
            conn=config.client['concurrency'],
            requests=config.client['requests'],
            cpu=cpu_limit,
            result_path=config.results_dir)
    print("client cmd", client_cmd)
    return client_cmd


def moongen_run(config, cpu_limit=0):
    config = VhostConf(config)
    MOON_HOME = config.moongen_home
    
    server_cmd = moongen_server_command_maker(config, cpu_limit)
    client_cmd = moongen_client_command_maker(config, cpu_limit)

    subprocess.call("sudo pkill MoonGen", cwd=MOON_HOME, shell=True)
    sleep(5)
    server_process = subprocess.Popen(['MOON_HOME=' + MOON_HOME + ';cd $MOON_HOME; sudo  ' + server_cmd], shell=True)
    if(cpu_limit):
        print("CPU_LIM")
        sleep(5)
        subprocess.Popen(["a=`pidof MoonGen`; sudo cpulimit -p $a -l " + str(cpu_limit) + ";"], shell=True)
    sleep(5)
    subprocess.check_call(client_cmd, cwd=MOON_HOME, shell=True)
    subprocess.check_call('for i in `pidof MoonGen`; do sudo kill -15 $i ; break; done', shell=True)
    subprocess.call("sudo pkill cpulimit", shell=True)
    sleep(10)
    print("Moongen exits")

def make_up_the_results_dir(iconfig):
    from datetime import datetime
    config = VhostConf(iconfig)
    now = datetime.now()
    dt_string = now.strftime("%d-%m-%Y-%H-%M-%S")
    new_path = os.path.join(config.results_dir, dt_string)
    subprocess.run(['mkdir', '-p', new_path])
    return new_path
    

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


def redraw_latency_drop_plot():
    config = VhostConf(load_exp_conf("config/exp_config.json"))
    draw_latency_drop_plots(config.plot)

def sysbench(config):
    cmd = "taskset --cpu-list 16 sysbench --test=cpu --cpu-max-prime=200000000 --num-threads=%s run &" % config[
        "thread_number"]
    subprocess.call(cmd, shell=True)


def kill_sysbench():
    cmd = "pkill sysbench"
    subprocess.check_call(cmd, shell=True)


def run_beta(config_path):
    config = VhostConf(load_exp_conf(config_path))
    general_config = VhostConf(config.general)

    if(general_config.sysbench):
        sysbench(config.sysbench)

    if(general_config.bess):
        bess(config.bess)

    if(general_config.moongen):
        moongen_run(config.moongen, 0)

    if(general_config.mtcp):
        mtcp_run(config.mtcp)

    if(general_config.sysbench):
        kill_sysbench()

def run(config_path, cpu_limit):
    config = VhostConf(load_exp_conf(config_path))
    general_config = VhostConf(config.general)

    if(general_config.sysbench):
        sysbench(config.sysbench)

    if(general_config.bess):
        bess(config.bess)

    if(general_config.moongen):
        make_up_the_results_dir(config.moongen)
        moongen_run(config.moongen, cpu_limit)

    if(general_config.mtcp):
        mtcp_run(config.mtcp)

    if(general_config.sysbench):
        kill_sysbench()

def run_alpha(config, cpu_limit):
    general_config = VhostConf(config.general)

    if(general_config.sysbench):
        sysbench(config.sysbench)

    if(general_config.bess):
        bess(config.bess)

    if(general_config.moongen):
        moongen_run(config.moongen, cpu_limit)

    if(general_config.mtcp):
        mtcp_run(config.mtcp)

    if(general_config.sysbench):
        kill_sysbench()

    if(general_config.tas):
        pass

    if(general_config.tas_benchmakr):
        pass


def start_experiment(path):
    config = VhostConf(load_exp_conf(path))
    default_path = config.moongen["results_dir"]
    for i in range(10):
        config.moongen["results_dir"] = default_path
        new_path = make_up_the_results_dir(config.moongen)
        config.moongen["results_dir"] = new_path
        for i in range(5, 101, 5):
            run_alpha(config, i)



if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Software Switch Experiments')
    parser.add_argument('--path', type=str, default='config/tas/config.json', help='Absolute/relative path to the config file')
    args = parser.parse_args()
    run_beta(args.path)


