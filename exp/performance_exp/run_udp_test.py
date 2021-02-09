#!/usr/bin/python3
import sys
from time import sleep
import os
import argparse
import subprocess
import argparse

sys.path.insert(0, "../")
from bkdrft_common import *
# TODO: use run_udp_app instead of run_server and run_client
from tas_containers import spin_up_memcached, spin_up_tas

# For debuging
# print applications output directly into the stdout
DIRECT_OUTPUT = False

if DIRECT_OUTPUT:
    print('=' * 32)
    print(' ' * 5, 'direct output is ON')
    print('=' * 32)

# Select the port type
PMD = 0
VPORT = 1
# This is overwritten when parsing args
PORT_TYPE = PMD

cur_script_dir = os.path.dirname(os.path.abspath(__file__))
pipeline_config_file = os.path.join(cur_script_dir, 'exp_config.bess')
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
    content.insert(index, 'LOSSLESS = {}\n'.format(str(buffering)))
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
    cpu = ['2', '3'][instance]  # on which cpu
    server_delay = [0, slow][instance]
    args = {
            'bin': slow_receiver_exp,
            'cpu': cpu,
            'count_queue': count_queue,
            'sysmod': 'bess' if sysmod == 'bess-bp' else sysmod,
            'mode': 'server',
            'inst': instance,
            'delay': server_delay,
            'source_ip': _server_ips[instance],
            'bidi': 'false',
            }
    if PORT_TYPE == PMD:
        vdev = ['virtio_user0,path=/tmp/ex_vhost0.sock,queues='+str(count_queue),
                'virtio_user5,path=/tmp/bg_server_1.sock,queues='+str(count_queue)][instance]
        prefix = 'slow_receiver_server_{}'.format(instance)
        args['vdev'] = vdev
        args['file-prefix'] = prefix
        cmd = ('sudo {bin} --no-pci --lcores="{cpu}" --file-prefix={file-prefix} '
                '--vdev="{vdev}" --socket-mem=128 -- '
                'bidi={bidi} {source_ip} {count_queue} {sysmod} {mode} {delay}').format(**args)
    else:
        vdev = ['ex_vhost0','ex_vhost2'][instance]
        prefix = 'bessd-dpdk-prefix'
        args['vdev'] = vdev
        args['file-prefix'] = prefix
        cmd = ('sudo {bin} --no-pci --lcores="{cpu}" --file-prefix={file-prefix} '
                '--proc-type=secondary --socket-mem=128 -- '
                'bidi={bidi} vport={vdev} {source_ip} {count_queue} '
                '{sysmod} {mode} {delay}').format(**args)

    print("=" * 32)
    print(" " * 13 + "server")
    print(cmd)
    print("=" * 32, end='\n\n')
    # Run in background
    if not DIRECT_OUTPUT:
        p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
    else:
        p = subprocess.Popen(cmd, shell=True)
    return p


def run_client(instance):
    """
        Start a client process
    """
    port = [1008, 8989, 9002][instance]
    cpu = ['(4-5)', '(6-7)', '(8-9)'][instance]
    # TODO: the following line is an example of code that is not suitable!
    # should switch to run_udp_app instead of this function
    ips = [[_server_ips[0], ],  # latency server ip
           [_server_ips[1]],
           [_server_ips[1]]][instance]
    rate = [1000, 1500000, -1][instance]
    _ips = ' '.join(ips)
    _cnt_flow = [1, count_flow, count_flow][instance]
    delay = [0, 0, 0]  # cycles per packet
    args = {
            'bin': slow_receiver_exp,
            'cpu': cpu,
            'count_queue': count_queue,
            'sysmod': 'bess' if sysmod == 'bess-bp' else sysmod,
            'mode': 'client',
            'cnt_ips': len(ips),
            'ips':  _ips,
            'count_flow': _cnt_flow,
            'duration': duration,
            'source_ip': _client_ip[instance],
            'port': port,
            'delay': delay[instance],
            'bidi': 'false',
            }
    if PORT_TYPE == PMD:
        vdev = ['virtio_user1,path=/tmp/ex_vhost1.sock,queues='+str(count_queue),
               'virtio_user4,path=/tmp/bg_client_1.sock,queues='+str(count_queue),][instance]
        prefix = 'slow_receiver_exp_client_{}'.format(instance)
        args['vdev'] = vdev
        args['file-prefix'] = prefix
        cmd = ('sudo {bin} --no-pci --lcores="{cpu}" --file-prefix={file-prefix} '
                '--vdev="{vdev}" --socket-mem=128 -- '
                'bidi={bidi} {source_ip} {count_queue} {sysmod} {mode} {cnt_ips} {ips} '
                '{count_flow} {duration} {port} {delay}').format(**args)
    else:
        vdev = ['ex_vhost1', 'ex_vhost3', 'ex_vhost4'][instance]
        prefix = 'bessd-dpdk-prefix'
        args['vdev'] = vdev
        args['file-prefix'] = prefix
        cmd = ('sudo {bin} --no-pci --lcores="{cpu}" --file-prefix={file-prefix} '
                '--proc-type=secondary --socket-mem=128 -- '
                'bidi={bidi} vport={vdev} {source_ip} {count_queue} '
                '{sysmod} {mode} {cnt_ips} {ips} '
                '{count_flow} {duration} {port} {delay}').format(**args)
    if rate >= 0:
        # add rate limit argument
        cmd += ' {}'.format(rate)

    print("=" * 32)
    print(" " * 13 + "client")
    print(cmd)
    print("=" * 32, end='\n\n')

    # Run in background
    if not DIRECT_OUTPUT:
        p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
    else:
        p = subprocess.Popen(cmd, shell=True)
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
      memcached
      tas_rpc
      UDP
      UDP Background
    """

    memcached_config = [{
        'memory': 1024,
        'threads': 1,
        'name': 'tas_mem_server_1',
        'type': 'server',
        'image': 'tas_memcached',
        'cpu': '10,11,12',
        'socket': '/tmp/tas_mem_server_1.sock',
        'ip': '10.10.1.4',
        'prefix': 'tas_mem_server_1',
        'cpus': 3,
        'tas_cores': 1,
        'tas_queues': count_queue,
        'cdq': int(cdq),
        }, {
            'dst_ip': '10.10.1.4',
            'duration': duration,
            'warmup_time': 0,
            'wait_before_measure': 0,
            'threads': 1,
            'connections': 8,
            'name': 'tas_mem_client_1',
            'type': 'client',
            'image': 'tas_memcached',
            'cpu': '13,14,15',
            'socket': '/tmp/tas_mem_client_1.sock',
            'ip': '172.20.1.2',
            'prefix': 'tas_mem_client_1',
            'cpus': 3,
            'tas_cores': 1,
            'tas_queues': count_queue,
            'cdq': int(cdq),
            }
        ]

    rpc_config = [
            {   # server 1
                'port': 1234,
                'count_flow': 100,
                'ips': [('10.0.0.2', 1234),],  # not used for server
                'flow_duration': 0,  # ms # not used for server
                'message_per_sec': -1,  # not used for server
                'message_size': 200,
                'flow_num_msg': 0,   # not used for server
                'count_threads': 1,
                'name': 'rpc_server_1',
                'type': 'server',
                'image': 'tas_container',
                'cpu': '16,17,18',
                'socket': '/tmp/rpc_server_1.sock',
                'ip': '10.10.1.6',
                'prefix': 'rpc_server_1',
                'cpus': 3,
                'tas_cores': 1,
                'tas_queues': count_queue,
                'cdq': int(cdq),
                }, {   # client 1
                    'name': 'rpc_client_1',
                    'type': 'client',
                    'image': 'tas_container',
                    'cpu': '19,1,8',
                    'socket': '/tmp/rpc_client_1.sock',
                    'ip': '172.40.1.2',
                    'prefix': 'rpc_client_1',
                    'cpus': 3,
                    'tas_cores': 1,
                    'tas_queues': count_queue,
                    'cdq': int(cdq),
                    'port': 7788,  # not used for client
                    'count_flow': 4,
                    'ips': [('10.10.1.6', 1234)],
                    'flow_duration': 0,
                    'message_per_sec': -1,
                    'message_size': 300,
                    'flow_num_msg': 0,
                    'count_threads': 1,
                    },
                ]


    # Kill anything running
    print('stop any thing running from previous tries')
    subprocess.run('sudo pkill udp_app', shell=True)  # Stop server

    for c in (memcached_config + rpc_config):
        name = c.get('name')
        subprocess.run('sudo docker stop -t 0 {}'.format(name),
                shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        subprocess.run('sudo docker rm {}'.format(name),
                shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    bessctl_do('daemon stop', stderr=subprocess.PIPE)
    sleep(1)

    # Run bess daemon
    print('start bess daemon')
    ret = bessctl_do('daemon start')
    if ret.returncode != 0:
        # TODO: this way of checking for failed pipeline does not work
        # look for the bug or find a better way.
        print('failed to start bess daemon')
        return 1

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

    count_client = 1
    clients = []

    # Run server
    server_p1 = run_server(0)
    mem_server = spin_up_memcached(memcached_config[0])
    rpc_server = spin_up_tas(rpc_config[0])
    bg_server = run_server(1)
    sleep(3)
    # Run client
    client_p = run_client(0)
    mem_client = spin_up_memcached(memcached_config[1])
    rpc_client = spin_up_tas(rpc_config[1])
    bg_client = run_client(1)

    # Wait
    client_p.wait()
    bg_client.wait()
    while docker_container_is_running(memcached_config[1]['name']):
        sleep(3)
    server_p1.wait()
    bg_server.wait()

    # Get output of processes
    if not DIRECT_OUTPUT:
        print('++++++ Latency Client ++++')
        txt = str(client_p.stdout.read().decode())
        print(txt)
        print('++++++ Incast Client ++++')
        txt = get_docker_container_logs(memcached_config[1]['name'])
        print(txt)
        print('++++++ Background Client ++++')
        txt = str(bg_client.stdout.read().decode())
        print(txt)
        print('++++++ RPC Client ++++')
        txt = get_docker_container_logs(rpc_config[1]['name'])
        print(txt)
        print('++++++ Latency Server ++++')
        txt = str(server_p1.stdout.read().decode())
        print(txt)
        txt = str(server_p1.stderr.read().decode())
        print(txt)
        print('++++++ Incast Server ++++')
        txt = get_docker_container_logs(memcached_config[0]['name'])
        print(txt)
        print('++++++ Background Server ++++')
        txt = str(bg_server.stdout.read().decode())
        print(txt)
        txt = str(bg_server.stderr.read().decode())
        print(txt)
        print('++++++ RPC Server ++++')
        txt = get_docker_container_logs(rpc_config[0]['name'])
        print(txt)
        print('+++++++++++++++++++')

    print('----- switch stats -----')
    print('Latency Server\n')
    p = bessctl_do('show port ex_vhost0', stdout=subprocess.PIPE)
    txt = p.stdout.decode()
    print(txt)

    print('Incast Server\n')
    p = bessctl_do('show port ex_vhost2', stdout=subprocess.PIPE)
    txt = p.stdout.decode()
    print(txt)

    print('Background Server\n')
    p = bessctl_do('show port bg_server', stdout=subprocess.PIPE)
    txt = p.stdout.decode()
    print(txt)

    print('RPC Server\n')
    p = bessctl_do('show port rpc_server', stdout=subprocess.PIPE)
    txt = p.stdout.decode()
    print(txt)

    print('Latency Client\n')
    p = bessctl_do('show port ex_vhost1', stdout=subprocess.PIPE)
    txt = p.stdout.decode()
    print(txt)

    print('Incast Client\n')
    p = bessctl_do('show port ex_vhost3', stdout=subprocess.PIPE)
    txt = p.stdout.decode()
    print(txt)

    print('Background Client\n')
    p = bessctl_do('show port bg_client', stdout=subprocess.PIPE)
    txt = p.stdout.decode()
    print(txt)

    print('RPC Client\n')
    p = bessctl_do('show port rpc_client', stdout=subprocess.PIPE)
    txt = p.stdout.decode()
    print(txt)

    # bessctl_do('command module client_qout0 get_pause_calls EmptyArg {}')
    FNULL = open(os.devnull, 'w') # pipe output to null
    bessctl_do('command module server1_qout get_pause_calls EmptyArg {}',
            stdout=FNULL)
    bessctl_do('command module server2_qout get_pause_calls EmptyArg {}',
            stdout=FNULL)
    bessctl_do('command module server3_qout get_pause_calls EmptyArg {}',
            stdout=FNULL)
    bessctl_do('command module server4_qout get_pause_calls EmptyArg {}',
            stdout=FNULL)
    bessctl_do('daemon stop', stdout=FNULL)

    # Kill docker containers
    for c in (memcached_config + rpc_config):
        name = c.get('name')
        subprocess.run('sudo docker stop -t 0 {}'.format(name),
                shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        subprocess.run('sudo docker rm {}'.format(name),
                shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    print_pps_from_info_log()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('count_queue', type=int)
    parser.add_argument('mode',
      help='define whether bess or bkdrft system should be used')
    parser.add_argument('delay', type=int,
      help='processing cost for each packet in cpu cycles '
           '(for both client and server)')
    parser.add_argument('--count_flow', type=int, default=1,
      help='number of flows, per each core. used for client app')
    parser.add_argument('--cdq', action='store_true', default=False,
      help='enable command data queueing')
    parser.add_argument('--bp', action='store_true', default=False,
      help='have pause call (backpresure) enabled')
    parser.add_argument('--pfq', action='store_true', default=False,
      help='enable per flow queueing')
    parser.add_argument('--buffering', action='store_true', default=False,
      help='buffer packets (no drop)')
    parser.add_argument('--duration', type=int, default=10,
      help='experiment duration')
    parser.add_argument('--bessonly', action='store_true', default=False)
    parser.add_argument('--port_type', choices=('pmd', 'bdvport'),
      default='pmd', help='type of port used in pipeline configuration')

    args = parser.parse_args()
    count_queue = args.count_queue
    sysmod = args.mode
    count_flow = args.count_flow
    slow = args.delay
    bess_only = args.bessonly
    bp = args.bp
    cdq = args.cdq
    pfq = args.pfq
    buffering = args.buffering
    duration = args.duration

    if args.port_type == 'pmd':
        print('Port type: PMD')
        PORT_TYPE = PMD
    else:
        print('Port type: BDVPort')
        PORT_TYPE = VPORT

    # Point to the relevant pipeline configuration based on port type
    # TODO: use json config file instead of genrating .bess pipeline files
    if PORT_TYPE == VPORT:
        pipeline_config_temp = os.path.join(cur_script_dir,
                                            'vport_pipeline.txt')
    else:
        pipeline_config_temp = os.path.join(cur_script_dir,
                                            'pmd_port_pipeline.txt')

    # TODO: having const ips does not scale
    _server_ips = ['10.10.1.3', '10.10.1.5']
    _client_ip = ['172.10.1.2', '172.30.1.2']

    if cdq and sysmod != 'bkdrft':
        print('comand data queueing is only available on bkdrft mode',
              file=sys.stderr)
        sys.exit(1)

    if cdq and count_queue < 2:
        print('command data queueing needs at least 2 queues', file=sys.stderr)
        sys.exit(1)

    if sysmod == 'bkdrft' and not cdq:
       print('bkdrft needs command data queueing', file=sys.stderr)
       sys.exit(1)

    if bp and not buffering:
        print("Backpressure needs buffering to be enabled", file=sys.stderr)
        sys.exit(1)

    main()

