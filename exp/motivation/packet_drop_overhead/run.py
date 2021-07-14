#! /usr/bin/python3
import argparse
import os
import subprocess
import sys
from time import sleep

sys.path.insert(0, '../../')
from bkdrft_common import *
from tas_containers import *


cur_script_dir = os.path.dirname(os.path.abspath(__file__))
pipeline_config_file = os.path.join(cur_script_dir,
    'pipeline.bess')


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--client', action='store_true')
    args = parser.parse_args()
    return args


def main():
    if os.geteuid() != 0:
        print('Need root permision to run TAS and ...')
        return 1
    args = parse_args()
    ret = setup_bess_pipeline(pipeline_config_file)
    if not ret:
        print('Failed to setup BESS')
        return 1
    print('Bess pipeline setup')
    sleep(1)

    server_ip = '192.168.1.2'
    server_port = 8080
    log_file = './tmp/client_log.txt'

    msgsz = 64
    pending = 32

    current_ip = '192.168.1.3' if args.client else server_ip
    tas_proc = setup_tas_engine({
        'dpdk--vdev': 'virtio_user0,path=/tmp/tmp_vhost0.sock,queues=1',
        'dpdk--no-pci': True,
        'ip-addr': current_ip,
        'fp-cores-max': 1,
        'fp-no-xsumoffload': True,
        'fp-no-autoscale': True,
        'fp-no-ints': True,
        }, cpuset='4,6,8')
    # tas_proc = setup_tas_engine({
    #     'dpdk-w': '41:00.0',
    #     'ip-addr': current_ip,
    #     'fp-cores-max': 1,
    #     })
    sleep(3)
    if tas_proc.returncode is not None:
        print ('Failed to setup TAS')
        return 1
    print ('TAS engine is up')

    libinterpose = os.path.abspath(
            '../../../code/tas/lib/libtas_interpose.so')
    tas_app_dir = os.path.abspath(
            '../../../code/tas-benchmark/micro_rpc_modified/')
    if args.client:
        print('client')
        binary = os.path.join(tas_app_dir, 'testclient_linux')
        app_cmd = '{} 1 {}:{} 1 foo {} {} {} 1'.format(binary,
                server_ip, server_port, log_file, msgsz, pending)
    else:
        print('server')
        binary = os.path.join(tas_app_dir, 'echoserver_linux')
        app_cmd = '{} {} 1 foo 1024 64'.format(binary, server_port)
    cmd = 'sudo LD_PRELOAD={} {}'.format(libinterpose, app_cmd)
    try:
        subprocess.run(cmd, shell=True)
    except KeyboardInterrupt as e:
        pass

    tas_proc.send_signal(subprocess.signal.SIGINT)
    ret = bessctl_do('daemon stop', stderr=subprocess.PIPE)
    if ret.stderr is not None:
        print('Failed to stop BESS daemon')
        return 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as e:
        print(e)
        subprocess.run('sudo pkill --signal SIGINT tas', shell=True)
        subprocess.run('sudo pkill --signal SIGINT echoserver', shell=True)

