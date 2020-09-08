#!/usr/bin/python3
import sys
from time import sleep
import os
import argparse
import subprocess
import argparse

sys.path.insert(0, "../")
from bkdrft_common import *


cur_script_dir = os.path.dirname(os.path.abspath(__file__))
bessctl_dir = os.path.abspath(os.path.join(cur_script_dir, '../../code/bess-v1/bessctl'))
bessctl_bin = os.path.join(bessctl_dir, 'bessctl')

pipeline_config_temp = os.path.join(cur_script_dir, 'pipeline_config_template.txt')
pipeline_config_file = os.path.join(cur_script_dir, 'slow_receiver.bess')

slow_receiver_exp = os.path.abspath(os.path.join(cur_script_dir,
        '../../code/apps/udp_client_server/build/udp_app'))


def update_config():
    with open(pipeline_config_temp, 'r') as f:
        content = f.readlines()
    index = content.index('# == Insert here ==\n') + 1
    if sysmod == 'bess':
        agent = 'BESS'
    elif sysmod == 'bkdrft':
        agent = 'BKDRFT'
    else:
        agent = 'BESS_BP'

    content.insert(index, 'AGENT = {}\n'.format(agent))
    content.insert(index, 'BP = {}\n'.format(str(bp)))
    content.insert(index, 'COMMAND_Q = {}\n'.format(str(cdq)))
    content.insert(index, 'CNT_Q = {}\n'.format(str(count_queue)))
    content.insert(index, 'PFQ = {}\n'.format(str(pfq)))
    content.insert(index, 'LOSSLESS = {}\n'.format(str(lossless)))
    with open(pipeline_config_file, 'w') as f:
        f.writelines(content)


def remove_socks():
    """
    In .bess file vdev files are placed in /tmp/*.sock
    this function removes all of them
    ------------------------------------------------
    This function may not be safe
    """
    subprocess.run('sudo rm /tmp/*.sock -f', shell=True)


def run_server(instance):
    """
        Start a server process
    """
    prefix = 'slow_receiver_server_{}'.format(instance)
    cpu = ['2', '3'][instance]  # on which cpu
    vdev = ['virtio_user0,path=/tmp/ex_vhost0.sock,queues='+str(count_queue),
            'virtio_user2,path=/tmp/ex_vhost2.sock,queues='+str(count_queue)][instance]
    server_delay = [0, slow][instance]
    # ip = '192.168.1.2'
    args = {
            'bin': slow_receiver_exp,
            'cpu': cpu,
            'file-prefix': prefix,
            'vdev': vdev,
            'count_queue': count_queue,
            'sysmod': 'bess' if sysmod == 'bess-bp' else sysmod,
            'mode': 'server',
            'inst': instance,
            'delay': server_delay,
            'source_ip': _server_ip,
            }
    cmd = ('sudo {bin} --no-pci -l{cpu} --file-prefix={file-prefix} '
            '--vdev="{vdev}" --socket-mem=128 -- '
            '{source_ip} {count_queue} {sysmod} {mode} {delay}').format(**args)

    print("===============")
    print("     server    ")
    print(cmd)
    print("===============")
    # Run in background
    p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return p


def run_client(instance):
    """
        Start a client process

        First 20 seconds of the client runtime is not counted in
        percentiles report. (Consult the .../apps/.../client.c source code)
    """
    port = [1001, 5001,][instance]
    prefix = 'slow_receiver_exp_client_{}'.format(instance)
    cpu = [5, 6][instance]
    vdev = ['virtio_user1,path=/tmp/ex_vhost1.sock,queues='+str(count_queue),
           'virtio_user3,path=/tmp/ex_vhost3.sock,queues='+str(count_queue),][instance]
    ips = [[_server_ip],[_server_ip]][instance]
    _ips = ' '.join(ips)
    _cnt_flow = [4, count_flow][instance]
    args = {
            'bin': slow_receiver_exp,
            'cpu': cpu,
            'file-prefix': prefix,
            'vdev': vdev,
            'count_queue': count_queue,
	          'sysmod': 'bess' if sysmod == 'bess-bp' else sysmod,
            'mode': 'client',
            'cnt_ips': len(ips),
            'ips':  _ips,
            'count_flow': _cnt_flow,
            'duration': duration,
            'source_ip': _client_ip,
            'port': port,
            }
    cmd = ('sudo {bin} --no-pci -l{cpu} --file-prefix={file-prefix} '
            '--vdev="{vdev}" --socket-mem=128 -- '
            '{source_ip} {count_queue} {sysmod} {mode} {cnt_ips} {ips} '
            '{count_flow} {duration} {port}').format(**args)

    print("===============")
    print("     client")
    print(cmd)
    print("===============")

    # Run in background
    p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
    return p


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
        BKDRFT UDP Testbed
    """

    # Kill anything running
    subprocess.run('sudo pkill udp_app', shell=True)  # Stop server
    bessctl_do('daemon stop') # this may show an error it is nothing

    # Run bess daemon
    print('start bess daemon')
    sleep(1)
    ret = bessctl_do('daemon start')
    if ret.returncode != 0:
        print('failed to start bess daemon')
        return 1

    #sleep(2)

    print('==============================')
    print('         UDP Testbed')
    print('==============================')

    # Update configuration
    update_config()

    # Run a configuration (pipeline)
    remove_socks()
    file_path = pipeline_config_file
    ret = bessctl_do('daemon start -- run file {}'.format(file_path))

    if bess_only:
        # Only run bess config
        return 0

    # Run server
    server_p1 = run_server(0)
    sleep(3)
    # server_p2 = run_server(1)
    # Run client
    client_p = run_client(0)
    sleep(3)
    client_p2 = run_client(1)

    # Wait
    client_p.wait()
    client_p2.wait()
    # subprocess.run('sudo pkill udp_app', shell=True)  # Stop server
    # server_p1.kill()
    server_p1.wait()
    # server_p2.wait()


    # Get output of processes
    print('====== client1 ====')
    txt = str(client_p.stdout.read().decode())
    print(txt)
    print('====== client2 ====')
    txt = str(client_p2.stdout.read().decode())
    print(txt)
    print('====== server ====')
    txt = str(server_p1.stdout.read().decode())
    print(txt)
    txt = str(server_p1.stderr.read().decode())
    print(txt)
    print('======')
    # print('======')
    # txt = str(server_p2.stdout.read().decode())
    # print(txt)
    # print('======')

    print('server1\n')
    p = bessctl_do('show port ex_vhost0', stdout=subprocess.PIPE)
    txt = p.stdout.decode()
    print(txt)

    print('server2\n')
    p = bessctl_do('show port ex_vhost2', stdout=subprocess.PIPE)
    txt = p.stdout.decode()
    print(txt)

    print('client\n')
    p = bessctl_do('show port ex_vhost1', stdout=subprocess.PIPE)
    txt = p.stdout.decode()
    print(txt)
    print('client2\n')
    p = bessctl_do('show port ex_vhost3', stdout=subprocess.PIPE)
    txt = p.stdout.decode()
    print(txt)
    # txt = p.stdout.decode()
    # bessctl_do('command module client_qout0 get_pause_calls EmptyArg {}')
    bessctl_do('command module server1_qout get_pause_calls EmptyArg {}')
    bessctl_do('command module server2_qout get_pause_calls EmptyArg {}')
    bessctl_do('daemon stop')

    print_pps_from_info_log()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('count_queue', type=int)
    parser.add_argument('mode', help='define whether bess or bkdrft system should be used')
    parser.add_argument('delay', type=int, help='delay of slow server in micro-seconds for each batch of packets')
    parser.add_argument('--count_flow', type=int, default=1, help='number of flows, per each core. used for client app')
    parser.add_argument('--cdq', action='store_true', default=False, help='enable command data queueing')
    parser.add_argument('--bp', action='store_true', default=False, help='have pause call (backpresure) enabled')
    parser.add_argument('--pfq', action='store_true', default=False, help='enable per flow queueing')
    parser.add_argument('--buffering', action='store_true', default=False, help='buffer packets (no drop)')
    parser.add_argument('--duration', type=int, default=10, help='experiment duration')
    parser.add_argument('--bessonly', action='store_true', default=False)

    args = parser.parse_args()
    count_queue = args.count_queue
    sysmod = args.mode
    count_flow = args.count_flow
    slow = args.delay
    bess_only = args.bessonly
    bp = args.bp
    cdq = args.cdq
    pfq = args.pfq
    lossless = args.buffering
    duration = args.duration

    _server_ip = '10.10.1.3'
    _client_ip = '10.10.1.2'

    if cdq and sysmod != 'bkdrft':
        print('comand data queueing is only available on bkdrft mode', file=sys.stderr)
        sys.exit(1)

    main()
