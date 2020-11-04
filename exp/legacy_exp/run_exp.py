#!/usr/bin/python3
import os
import sys
from time import sleep
import argparse
import subprocess
import json

sys.path.insert(0, '../')
from bkdrft_common import *


cur_script_dir = os.path.dirname(os.path.abspath(__file__))

pipeline_config_file = os.path.join(cur_script_dir,
    'pipeline.bess')

def _stop_everything(containers):
    try:
      bessctl_do('daemon stop') # this may show an error it is nothing
    except:
      # bess was not running
      pass
    for container in containers:
      cmd = 'sudo docker stop -t 0 {name}'
      cmd = cmd.format(**container)
      subprocess.run(cmd, shell=True, stdout=subprocess.PIPE)
      cmd = 'sudo docker rm {name}'
      cmd = cmd.format(**container)
      subprocess.run(cmd, shell=True, stdout=subprocess.PIPE)


def run_docker_container(container):
    cmd = 'sudo docker run --cpuset-cpus={cpuset} --cpus={cpu_share} -d --network none --name {name} {image} {args}'
    cmd = cmd.format(**container)
    print(cmd)
    subprocess.run(cmd, shell=True)


def main():
    # Load config file
    containers = json.load(open(exp_config_path))
    if cpus is not None:
        containers[2]['cpu_share'] = cpus

    # Write pipeline config file
    with open('.pipeline_config.json', 'w') as f:
      # TODO: instead of server and client, write config so it has ip address in it
      # this way it is not important who is server and who is client
      # also this way config file becomes the single source of information about ip address
      txt = json.dumps(containers)
      f.write(txt)

    # Kill anything running
    _stop_everything(containers)

    # load kernel module
    print('loading BESS kernel module')
    load_bess_kmod()

    # Check BESS kernel module is loaded
    cmd = 'lsmod | grep bess'  # this is not reliable
    p = subprocess.run(cmd, shell=True, stdout=subprocess.PIPE)
    result = p.stdout.decode()
    print(result)
    if not result:
      print('BESS kmod is not loaded please load this kernel module first')
      return 1

    # Run containers
    # first run servers
    for container in containers:
        if container['type'] == 'server':
            run_docker_container(container)
    # wait some time
    sleep(1)
    # run cliecnts
    for container in containers:
        if container['type'] == 'client':
            run_docker_container(container)

    # Setup BESS config
    file_path = pipeline_config_file
    ret = bessctl_do('daemon start -- run file {}'.format(file_path))

    print('all containers and BESS pipeline has been setuped')

    # Wait for client to finish
    # Note: there is an assumption that the second container is the client
    # TODO: Relax this assumption
    client_container_name = containers[1]['name']
    #client_pid = get_container_pid(client_container_name)
    while docker_container_is_running(client_container_name):
        sleep(1)

    # Gather experiment result
    ## iperf3 logs
    tmp_txt = ''
    try:
        subprocess.check_call('sudo docker cp {}:/root/iperf3.log .'.format(client_container_name), shell=True)
        subprocess.run('sudo chown $USER ./iperf3.log', shell=True)
        with open('iperf3.log', 'r') as logfile:
            tmp_txt = logfile.read()
    except:
        pass

    ## mutilate logs
    logs = get_docker_container_logs(client_container_name)

    logs = '\n'.join(['===  client ===', logs, '=== iperf ===', tmp_txt])
    if args.output:
        output_file = open(args.output, 'w')
        output_file.write(logs)
    else:
        print(logs)

    logs = get_docker_container_logs(containers[0]['name'])
    logs = '\n'.join(['===  server ===', logs])
    print(logs)

    p = bessctl_do('show port', subprocess.PIPE)
    print(p.stdout.decode())

    # Stop and clear test environment
    _stop_everything(containers)


if __name__ == '__main__':
    supported_exps = ('iperf3', 'apache', 'memcached_iperf3',
                      'memcached_shuffle')
    parser = argparse.ArgumentParser()
    parser.add_argument('experiment', choices=supported_exps)
    parser.add_argument('--output', default=None, required=False,
        help="results will be writen in the given file")
    parser.add_argument('--cpus', type=float, default=None)
    args = parser.parse_args()

    exp = args.experiment
    exp_config_path = 'exp_configs/{}.json'.format(exp)
    cpus = args.cpus
    main()

