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
    'ecn_pipeline.bess')


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--client', action='store_true', help='run client')
    parser.add_argument('--ccwnd', action='store_true', help='constant congestion window')
    parser.add_argument('--cores', type=int, default=1, help='number of server cores')
    parser.add_argument('--cpulimit', type=float, default=1,
            help='what fraction of cpu the application can use (0, 1]')
    parser.add_argument('--duration', type=int, default=-1)
    parser.add_argument('--server-ip', type=str, default='192.168.1.1')
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
    tas_cores = 2
    bess_cores = 2
    if os.geteuid() != 0:
        print('Need root permision to run TAS and ...')
        return 1
    args = parse_args()
    if args.cpulimit != 1:
        check_cpulimit()

    ret = setup_bess_pipeline(pipeline_config_file,
            env={'cores': str(bess_cores), 'queues': str(tas_cores)})
    if not ret:
        print('Failed to setup BESS')
        return 1
    print('Bess pipeline setup')
    sleep(1)

    server_ip = args.server_ip
    server_port = 8080
    log_file = './tmp/client_log.txt'

    msgsz = 64
    pending = 16
    pps = -1  # Not working well
    client_threads = 8
    client_conns = 16 

    current_ip = '192.168.1.3' if args.client else server_ip
    tas_params = {
        'dpdk--vdev': 'virtio_user0,path=/tmp/tmp_vhost0.sock,queues={}'.format(tas_cores),
        'dpdk--no-pci': True,
        'ip-addr': current_ip,
        'fp-cores-max': tas_cores,
        'fp-no-xsumoffload': True,
        'fp-no-autoscale': True,
        'fp-no-ints': True,
        'count-queue': tas_cores,
        }
    if args.ccwnd:
        tas_params['cc'] = 'const-rate'
    cpuset = ','.join([str(bess_cores + i) for i in range(tas_cores + 1)])
    tas_proc = setup_tas_engine(tas_params, cpuset=cpuset)
    sleep(3)
    if tas_proc.returncode is not None:
        print ('Failed to setup TAS')
        return 1
    print ('TAS engine is up')

    libinterpose = os.path.abspath(
            '../../../code/tas/lib/libtas_interpose.so')
    tas_app_dir = os.path.abspath(
            '../../../code/tas-benchmark/micro_rpc_modified/')
    cores = args.cores
    if args.client:
        cores = client_threads
        print('client')
        binname = 'testclient_linux'
        binary = os.path.join(tas_app_dir, binname)
        app_args = '1 {}:{} {} foo {} {} {} {} 0 {}'.format(server_ip,
                server_port, cores, log_file, msgsz, pending,
                client_conns, pps)
    else:
        print('server')
        binname = 'echoserver_linux'
        binary = os.path.join(tas_app_dir, binname)
        app_args = '{} {} foo 1024 64'.format(server_port, cores)
    app_cmd = '{} {}'.format(binary, app_args)
    app_cpuset = ','.join([str(bess_cores + tas_cores + i) for i in range(cores)])
    cmd = 'LD_PRELOAD={} taskset -c {} {}'.format(libinterpose, app_cpuset, app_cmd)
    try:
        # print ('app cmd:', cmd)
        sh_proc = subprocess.Popen(cmd, shell=True)
        if args.cpulimit != 1:
            pid = int(subprocess.check_output('pidof -s tas', shell=True))
            percent = int(args.cores * args.cpulimit * 200)
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
        print(e.with_traceback(e))
        subprocess.run('sudo pkill --signal SIGINT tas', shell=True)
        subprocess.run('sudo pkill --signal SIGINT echoserver', shell=True)

