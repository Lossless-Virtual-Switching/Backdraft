#! /usr/bin/python3
import argparse
import os
import subprocess
import sys
from time import sleep

sys.path.insert(0, '../')
from bkdrft_common import *
from tas_containers import *

BD = 1

cur_script_dir = os.path.dirname(os.path.abspath(__file__))
if BD <= 0:
    pipeline_config_file = os.path.join(cur_script_dir,
        'pipeline.bess')
else:
    pipeline_config_file = os.path.join(cur_script_dir,
        'bd_pipeline.bess')
libinterpose = os.path.abspath(os.path.join(cur_script_dir,
        '../../code/tas/lib/libtas_interpose.so'))
tas_app_dir = os.path.abspath(os.path.join(cur_script_dir,
        '../../code/tas-benchmark/micro_rpc_modified/'))


# Some parameters
tas_cores = 1
tas_queues = 2
bess_cores = 1
server_ip = '192.168.1.1'
client_ip = '192.168.1.2'
server_port = 8080
log_file = './tmp/client_log.txt'
msgsz = 1500
pending = 128
pps = -1  # Not working well
client_conns = 1
client_cores = 1
server_cores = 1

if BD == 1 and tas_cores > 1:
    print('TAS should have only one core if using dorbell queues')
    sys.exit(1)
if BD != 1 and tas_cores != tas_queues:
    print('TAS cores should be same as TAS queues (if not using dorbell queues)')
    sys.exit(1)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cores', type=int, default=1, help='number of server cores')
    parser.add_argument('--duration', type=int, default=5)
    args = parser.parse_args()
    return args


# Reserving first few cores for Bess
_last_core = bess_cores
_max_cores = os.cpu_count()
def get_cpuset(num_cores):
    global _last_core
    if _last_core + num_cores > _max_cores:
        raise ValueError('Not enough cores')
    cpuset = ','.join(
            [str(i) for i in range(_last_core, _last_core + num_cores)])
    _last_core += num_cores
    return cpuset


def setup_exp_tas(current_ip, tas_cores, file_prefix='_m1'):
    # Setup tas for server
    cpuset = get_cpuset(tas_cores)
    tas_params = {
        'dpdk--vdev': 'virtio_user0,path=/tmp/tmp_vhost0.sock,queues={}'.format(tas_cores),
        'dpdk--no-pci': True,
        'dpdk--file-prefix': file_prefix,
        'fp-file-prefix': file_prefix,
        'ip-addr': current_ip,
        'fp-cores-max': tas_cores,
        'fp-no-xsumoffload': True,
        'fp-no-autoscale': True,
        'fp-no-ints': True,
        'count-queue': tas_queues,
        }
    tas_proc = setup_tas_engine(tas_params, cpuset=cpuset)
    return tas_proc


def use_two_tas(args):
    # Setup TAS for server
    server_tas_proc = setup_exp_tas(server_ip, tas_cores, 'server')
    # Setup TAS for client
    client_tas_proc = setup_exp_tas(client_ip, tas_cores, 'client')
    sleep(1)
    if (server_tas_proc.returncode is not None or
            client_tas_proc.returncode is not None):
        print ('Failed to setup TAS')
        return 1
    print ('TAS engine is up')
    # Bring up server app
    print('server')
    cores = server_cores
    binname = 'echoserver_linux'
    binary = os.path.join(tas_app_dir, binname)
    app_args = '{} {} foo 1024 {}'.format(server_port, cores, msgsz)
    app_cmd = '{} {}'.format(binary, app_args)
    # app_cpuset = ','.join([str(bess_cores * 2 + tas_cores * 2 + i * 2) for i in range(cores)])
    app_cpuset = get_cpuset(cores)
    server_cmd = 'LD_PRELOAD={} taskset -c {} {}'.format(libinterpose, app_cpuset, app_cmd)
    server_proc = subprocess.Popen(server_cmd, shell=True)
    # Bring up server app
    print('client')
    cores = client_cores
    binname = 'testclient_linux'
    binary = os.path.join(tas_app_dir, binname)
    app_args = '1 {}:{} {} foo {} {} {} {} 0 {}'.format(server_ip,
            server_port, cores, log_file, msgsz, pending,
            client_conns, pps)
    app_cmd = '{} {}'.format(binary, app_args)
    # app_cpuset = ','.join([str(bess_cores * 2 + tas_cores * 2 + i * 2) for i in range(cores)])
    app_cpuset = get_cpuset(cores)
    client_cmd = 'LD_PRELOAD={} taskset -c {} {}'.format(libinterpose, app_cpuset, app_cmd)
    client_proc = subprocess.Popen(client_cmd, shell=True)
    # Waiting for client to finish
    try:
        if args.duration > 0:
            try:
                client_proc.wait(args.duration)
            except:
                pid = subprocess.check_output('pidof testclient_linu', shell=True)
                if pid:
                    pid = int(pid)
                    os.kill(pid, subprocess.signal.SIGINT)
                else:
                    print('Failed to get client pid')
                pid = subprocess.check_output('pidof echoserver', shell=True)
                if pid:
                    pid = int(pid)
                    os.kill(pid, subprocess.signal.SIGINT)
                else:
                    print('Failed to get server pid')
                # subprocess.run('pkill testclient_linu', shell=True)
        else:
            client_proc.wait()
    except KeyboardInterrupt as e:
        pass
    except Exception as e:
        subprocess.run('pkill testclient_linu', shell=True)
        subprocess.run('pkill echoserver', shell=True)
        print(e)

    server_tas_proc.send_signal(subprocess.signal.SIGINT)
    client_tas_proc.send_signal(subprocess.signal.SIGINT)


def use_docker_tas(args):
    def stop_container(name):
        cmd = f'docker stop -t 0 {name} ; sudo docker rm {name}'
        subprocess.run(cmd, shell=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def show_log(title, logs):
        print('\n\n*', title, '\n')
        print(logs)
        print('~' * 40)

    container_cores = tas_cores + server_cores + 2 
    server_container_name = 'tas_server'
    server_conf = {
            'name': server_container_name,
            'type': 'server',
            'cpu': get_cpuset(container_cores),
            'socket': '/tmp/tmp_vhost0.sock',
            'ip': server_ip,
            'prefix': 'exp_tas_server',
            'cpus': container_cores,
            'port': 11211,
            'count_flow': 1024,
            'ips': [],
            'flow_duration': 0,
            'message_per_sec': -1,
            'tas_cores': tas_cores,
            'tas_queues': tas_queues,
            'cdq': BD,
            'message_size': msgsz,
            'flow_num_msg': 0,
            'count_threads': server_cores,
            }
    container_cores = tas_cores + client_cores + 2 
    client_container_name = 'tas_client'
    client_conf = {
            'name': client_container_name,
            'type': 'client',
            'cpu': get_cpuset(container_cores),
            'socket': '/tmp/tmp_vhost1.sock',
            'ip': client_ip,
            'prefix': 'exp_tas_client',
            'cpus': container_cores,
            'port': 11211,
            'count_flow': client_conns,
            'ips': [(server_ip, 11211),],
            'flow_duration': 0,
            'message_per_sec': -1,
            'tas_cores': tas_cores,
            'tas_queues': tas_queues,
            'cdq': BD,
            'message_size': msgsz,
            'flow_num_msg': 0,
            'count_threads': client_cores,
            }
    # Stop containers if already runnig
    stop_container(server_container_name)
    stop_container(client_container_name)
    sleep(2)
    # Start container
    spin_up_tas(server_conf)
    spin_up_tas(client_conf)
    sleep(3)
    # Wait for client container to exit
    try:
        sleep(args.duration)
    except KeyboardInterrupt:
        pass
    cmd = ('docker exec {} pkill -SIGINT testclient_linu'
        .format(client_container_name))
    subprocess.run(cmd, shell=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
    sleep(1)
    while docker_container_is_running(client_container_name):
        print('Something is wrong the experiment is not closed')
        sleep(2)
    # Get container logs
    logs = get_docker_container_logs(server_container_name)
    show_log('Server:', logs)
    logs = get_docker_container_logs(client_container_name)
    show_log('Clients:', logs)
    # Stop and remove containers
    stop_container(client_container_name)
    stop_container(server_container_name)


def main():
    if os.geteuid() != 0:
        print('Need root permision to run TAS and ...')
        return 1
    args = parse_args()
    # Setup pipeline
    ret = setup_bess_pipeline(pipeline_config_file,
            env={'cores': str(bess_cores), 'queues': str(tas_queues)})
    if not ret:
        print('Failed to setup BESS')
        return 1
    print('Bess pipeline setup')
    sleep(1)
    pipeline = bessctl_do('show pipeline')
    print(pipeline)
    # Setup TAS and Apps
    # use_two_tas(args)
    use_docker_tas(args)

    bessctl_do('show port')
    ret = bessctl_do('daemon stop', stderr=subprocess.PIPE)
    if ret.stderr is not None and ret.stderr:
        print('Failed to stop BESS daemon')
        return 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as e:
        print('Failure')
        subprocess.run('pkill testclient_linu', shell=True)
        subprocess.run('pkill echoserver', shell=True)
        subprocess.run('pkill tas', shell=True)
        bessctl_do('daemon stop', stderr=subprocess.PIPE)
        import traceback
        print(e)
        traceback.print_exc()

