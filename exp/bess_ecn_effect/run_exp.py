#!/usr/bin/python3
import sys
from time import sleep
import os
import argparse
import subprocess
import argparse


cur_script_dir = os.path.dirname(os.path.abspath(__file__))
bessctl_dir = os.path.abspath(os.path.join(cur_script_dir, '../../code/bess-v1/bessctl'))
bessctl_bin = os.path.join(bessctl_dir, 'bessctl')

pipeline_config_temp = os.path.join(cur_script_dir, 'template_pipeline.txt')
pipeline_config_file = os.path.join(cur_script_dir, 'pipeline.bess')


def bessctl_do(command, stdout=None):
    """
    """
    cmd = '{} {}'.format(bessctl_bin, command)
    ret = subprocess.run(cmd, shell=True, stdout=subprocess.PIPE)
    return ret


def update_config():
    with open(pipeline_config_temp, 'r') as f:
        content = f.readlines()
    index = content.index('# == Insert here ==\n') + 1
    content.insert(index, "ecn_threshold = {}\n".format(ecn_t))

    with open(pipeline_config_file, 'w') as f:
        f.writelines(content)


def remove_socks():
    """
    In .bess file vdev files are placed in /tmp/*.sock
    this function removes all of them
    ------------------------------------------------
    This function may not be safe
    """
    subprocess.run('sudo rm tmp_vhost/*.sock -f', shell=True)


def spin_up_tas(conf):
    assert conf['type'] in ('client', 'server')

    cmd = './spin_up_tas_container.sh {name} {image} {cpu} {socket} {type} {ip} {prefix} {cpus:.2f} {server_port} {count_flow}'.format(
            **conf)
    return subprocess.run(cmd, shell=True)


def print_pps_from_info_log():
    book = dict()
    with open('/tmp/bessd.INFO') as log_file:
        for line in log_file:
            if 'pcps' in line:
                raw = line.split()
                value = raw[5]
                name = raw[7]
                if name not in book:
                    book[name] = list()
                book[name].append(value)
    print("=== pause call per sec ===")
    for name in book:
        print(name)
        for i, value in enumerate(book[name]):
            print(i, value)
    print("=== pause call per sec ===")

def main():
    """
    About experiment:
        TCP Performance Test Using Tas (with an app).
        + BESS
        + BESS + bp
        + Per-Flow Input Queueing
        + PFIQ + bp
    """
    # 1
    count_cpu = 3
    cpus = count_cpu * ((100 - slow_percent) / 100)
    # TODO: experiment parameters should be implemented
    containers = [
            # server 1
            {'name': 'tas_server', 'image': 'tas_container', 'cpu':'14,4,6',
                'socket': 'tas_server_1.sock', 'type': 'server', 'ip': '10.0.0.1',
                'prefix': 'tas_server', 'cpus': cpus, 'server_port': '1234','count_flow': 1},
            # client
            {'name': 'tas_client', 'image': 'tas_container', 'cpu':'8,10,12',
                'socket': 'tas_client_1.sock', 'type': 'client', 'ip': '10.0.0.2',
                'prefix': 'tas_client', 'cpus': 3, 'server_port': '1234', 'count_flow': 1},
            ]
    test_duration = 2 * 60 # 10 min

    # Kill anything running
    bessctl_do('daemon stop') # this may show an error it is nothing

    # Stop and remove running containers
    for c in containers:
        name = c.get('name')
        subprocess.run('sudo docker stop {}'.format(name), shell=True)
        subprocess.run('sudo docker rm -v {}'.format(name), shell=True)

    # Run BESS daemon
    print('start BESS daemon')
    ret = bessctl_do('daemon start')
    if ret.returncode != 0:
        print('failed to start bess daemon')
        return 1

    # Update configuration
    update_config()

    # Run a configuration (pipeline)
    remove_socks()
    file_path = pipeline_config_file
    ret = bessctl_do('daemon start -- run file {}'.format(file_path))

    print('==============================')
    print('     TCP Performance Test')
    print('==============================')

    if args.bessonly:
        print('info: only setup bess pipeline')
        return 

    # Spin up TAS
    for c in containers:
        spin_up_tas(c)
        sleep(5)

    # TODO: sudo docker cp tas_client:/tmp/log_drop_client.txt .
    sleep (test_duration)

    subprocess.run('sudo docker cp tas_client:/tmp/log_drop_client.txt ./client_log.txt', shell=True)

    tp = p50 = p90 = p95 = p99 = p999 = p9999 = 0
    count = 0
    with open('./client_log.txt') as f:
        for line in f:
            if line.startswith('flow'):
                continue
            count += 1
            raw = line.split()
            tp += float(raw[1].split('=')[1])
            p50 = float(raw[3].split('=')[1])
            p90 = float(raw[5].split('=')[1])
            p95 = float(raw[7].split('=')[1])
            p99 = float(raw[9].split('=')[1])
            p999 = float(raw[11].split('=')[1])
            p9999 = float(raw[13].split('=')[1])
    print('===== avg results =====')
    print('TP:', tp / count)
    print('@50:', p50)
    print('@90:', p90)
    print('@95:', p95)
    print('@99:', p99)
    print('@99.9:', p999)
    print('@99.99:', p9999)
    print('=======================')


    # BESS port stats
    p = bessctl_do('show port tas_server_1', subprocess.PIPE)
    txt = p.stdout.decode()
    print('server1\n', txt)

    p = bessctl_do('show port tas_client_1', subprocess.PIPE)
    txt = p.stdout.decode()
    print('client\n', txt)

    # pause call per sec
    print_pps_from_info_log()


    # Stop containers
    for c in containers:
        name = c.get('name')
        subprocess.run('sudo docker stop {}'.format(name), shell=True)

    # Stop BESS daemon
    bessctl_do('daemon stop')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--ecn_t', type=int, required=False, default=32, help='ecn')
    parser.add_argument('--slow_by', type=float, required=False, default=0, help='slow receiver receives less cpy by this percentage')
    parser.add_argument('--bessonly', type=bool, required=False, default=False, help='only setup bess pipeline')

    args = parser.parse_args()
    ecn_t = args.ecn_t
    slow_percent = args.slow_by
    print ('ecn threshold: ', ecn_t)
    main()

