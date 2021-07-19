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
    parser.add_argument('--client', action='store_true', help='run client')
    parser.add_argument('--ccwnd', action='store_true', help='constant congestion window')
    parser.add_argument('--cpulimit', type=float, default=1,
            help='what fraction of cpu the application can use (0, 1]')
    parser.add_argument('--duration', type=int, default=-1)
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


def main():
    if os.geteuid() != 0:
        print('Need root permision to run TAS and ...')
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

    server_ip = '192.168.1.2'
    server_port = 8080
    log_file = './tmp/client_log.txt'

    msgsz = 64
    pending = 32
    pps = -1  # Not working well

    current_ip = '192.168.1.3' if args.client else server_ip
    tas_params = {
        'dpdk--vdev': 'virtio_user0,path=/tmp/tmp_vhost0.sock,queues=1',
        'dpdk--no-pci': True,
        'ip-addr': current_ip,
        'fp-cores-max': 1,
        'fp-no-xsumoffload': True,
        'fp-no-autoscale': True,
        'fp-no-ints': True,
        }
    if args.ccwnd:
        tas_params['cc'] = 'const-rate'
    tas_proc = setup_tas_engine(tas_params, cpuset='4,6,8')
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
        binname = 'testclient_linux'
        binary = os.path.join(tas_app_dir, binname)
        app_args = '1 {}:{} 1 foo {} {} {} 1 0 {}'.format(server_ip,
                server_port, log_file, msgsz, pending, pps)
    else:
        print('server')
        binname = 'echoserver_linux'
        binary = os.path.join(tas_app_dir, binname)
        app_args = '{} 1 foo 1024 64'.format(server_port)
    app_cmd = '{} {}'.format(binary, app_args)
    cmd = 'LD_PRELOAD={} taskset -c 10,12,14 {}'.format(libinterpose, app_cmd)
    try:
        # print ('app cmd:', cmd)
        sh_proc = subprocess.Popen(cmd, shell=True)
        if args.cpulimit != 1:
            pid = int(subprocess.check_output('pidof -s tas', shell=True))
            percent = int(args.cpulimit * 200)
            print('cpulimit:', percent)
            cmd = 'cpulimit -p {} -l {}'.format(pid, percent)
            cpulimit_proc = subprocess.Popen(cmd, shell=True)
        if args.duration > 0:
            try:
                sh_proc.wait(args.duration)
            except:
                # os.kill(pid, subprocess.signal.SIGINT)
                subprocess.run('pkill testclient_linu', shell=True)
        else:
            sh_proc.wait()
    except KeyboardInterrupt as e:
        pass
    except Exception as e:
        print(e)

    tas_proc.send_signal(subprocess.signal.SIGINT)
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
        subprocess.run('sudo pkill --signal SIGINT tas', shell=True)
        subprocess.run('sudo pkill --signal SIGINT echoserver', shell=True)

