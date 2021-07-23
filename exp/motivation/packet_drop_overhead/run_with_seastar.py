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

seastar_memcached = '/proj/uic-dcs-PG0/fshahi5/seastar/build/release/app/memcached/memcached'


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--client', action='store_true', help='run client')
    parser.add_argument('--cpulimit', type=float, default=1,
            help='what fraction of cpu the application can use (0, 1]')
    args = parser.parse_args()
    if args.cpulimit < 0 or args.cpulimit > 1:
        print('cpulimt shoud be in range of (0, 1]')
        sys.exit(1)
    return args


def check_cpulimit():
    ret = subprocess.run('which cpulimit', shell=True, stdout=subprocess.PIPE)
    if ret.returncode != 0:
        print('please install cpulimimt')
        sys.exit(1)


def print_client_command():
    print("Client flag is not implemented!"
            "\nFor running client install mutilate and use"
            "\nthis command:"
            "\n\t./mutilate -s node0 -t 30 -K 24 -V 24 -r 10 -w 5 -T 16 -c 1")


def main():
    if os.geteuid() != 0:
        print('Need root permision to run Seastar and ...')
        return 1
    args = parse_args()
    if args.cpulimit != 1:
        check_cpulimit()

    ret = setup_bess_pipeline(pipeline_config_file)
    if not ret:
        print('Failed to setup BESS')
        return 1
    print('Bess pipeline setup')
    sleep(1)

    cores = 1  # server cores
    release = 'release'
    stack = 'native'
    # coremask = ','.join([2 * i + 4 for i in range(cores)])
    # stack = 'posix'
    if args.client:
        print_client_command()
    else:
        memcd_bin = f'/proj/uic-dcs-PG0/fshahi5/tmp/seastar/build/{release}/apps/memcached/memcached'
        memcd_args = ['--port=11211', '--dhcp=0', '--host-ipv4-addr=192.168.1.1',
        '--netmask-ipv4-addr=255.255.255.0', f'--network-stack={stack}', '-m', '8G',
        '--dpdk-pmd', f'--smp={cores}']
        # cmd = ['/usr/bin/taskset', '-c', f'{coremask}',  memcd_bin] + memcd_args
        cmd = [memcd_bin] + memcd_args
        print('CMD:', cmd)
        memcd_proc = subprocess.Popen(cmd)
        if args.cpulimit != 1:
            limit = int(args.cpulimit * 100)
            print('limiting usage to', limit, 'percent')
            subprocess.Popen(['/usr/bin/cpulimit', '-p', f'{memcd_proc.pid}', '-l', f'{limit:.0f}'])
        try:
            memcd_proc.wait()
        except KeyboardInterrupt:
            memcd_proc.send_signal(subprocess.signal.SIGINT)

    bessctl_do('show port')
    ret = bessctl_do('daemon stop', stderr=subprocess.PIPE)
    if ret.stderr is not None:
        print('Failed to stop BESS daemon')
        return 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as e:
        print(e)
        subprocess.run('sudo pkill --signal SIGINT memcached', shell=True)
        subprocess.run('sudo pkill --signal SIGINT mutilate', shell=True)

