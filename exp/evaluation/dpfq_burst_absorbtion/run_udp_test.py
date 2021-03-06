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
# from tas_containers import run_udp_app


# For debuging
# print applications output directly into the stdout
DIRECT_OUTPUT = False


# Select the port type
PMD = 0
VPORT = 1
# This is overwritten when parsing args
PORT_TYPE = VPORT


cur_script_dir = os.path.dirname(os.path.abspath(__file__))
pipeline_config_file = os.path.join(cur_script_dir, 'slow_receiver.bess')
slow_receiver_exp = os.path.abspath(os.path.join(cur_script_dir,
    '../../code/apps/udp_client_server/build/udp_app'))


def port_type_name(port_type):
    if port_type == VPORT:
        return 'BD_Vport'
    elif port_type == PMD:
        return 'PMD Port'
    else:
        return 'Unknown'


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
    cpu = ['9', '10'][instance]  # on which cpu
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
            'bidi': 'false'
            }
    if PORT_TYPE == PMD:
        vdev = ['virtio_user0,path=/tmp/ex_vhost0.sock,queues='+str(count_queue),
                'virtio_user2,path=/tmp/ex_vhost2.sock,queues='+str(count_queue)][instance]
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
    cpu = ['(3,4)', '(5,6)', '(7,8)'][instance]
    # TODO: the following line is an example of code that is not suitable!
    # should switch to run_udp_app instead of this function
    # ips = [[_server_ips[1], _server_ips[0]],
    ips = [[_server_ips[0],],
            [_server_ips[0]],
            [_server_ips[0]]][instance]
    mpps = 1000 * 1000
    rate = [-2 * mpps, 2 * mpps, 6 * mpps][instance]
    _ips = ' '.join(ips)
    _cnt_flow = [1, count_flow, count_flow][instance]
    delay = [0, 0, 100]  # cycles per packet
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
                'virtio_user3,path=/tmp/ex_vhost3.sock,queues='+str(count_queue),][instance]
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


def kill():
    # Kill anything running
    print('stop any thing running from previous tries')
    subprocess.run('sudo pkill udp_app', shell=True)  # Stop server
    bessctl_do('daemon stop', stderr=subprocess.PIPE)
    sleep(1)

def main():
    """
    About experiment:
        BKDRFT UDP Testbed Single Node
    """
    kill()

    # Run bess daemon
    print('start bess daemon')
    ret = bessctl_do('daemon start')

    print('==============================')
    print('         UDP Testbed')
    print('==============================')

    update_config()

    # Run a configuration (pipeline)
    remove_socks()
    file_path = pipeline_config_file
    ret = bessctl_do('daemon start -- run file {}'.format(file_path),
            cpu_list='0,2')

    if bess_only:
        # Only run bess config
        return 0

    count_client = 1
    count_server = 1
    print ('Number of active clientes: ', count_client)
    clients = []
    servers = []

    # Run server
    for i in range(count_server):
        server_p = run_server(i)
        servers.append(server_p)
    sleep(1)

    # Run client
    # global duration
    for i in range(count_client):
        client_p = run_client(i)
        clients.append(client_p)
        # sleep(1)
        # duration -= 1  # next client has less time

    # Wait
    try:
        for proc in clients + servers:
            proc.wait()
    except KeyboardInterrupt:
        # subprocess.run('sudo pkill udp_app', shell=True)  # Stop server
        for p in clients + servers:
            p.kill()

    # Get output of processes
    if not DIRECT_OUTPUT:
        for i, proc in enumerate(clients):
            print('++++++ client{} ++++'.format(i))
            txt = str(proc.stdout.read().decode())
            print(txt)
        for i, proc in enumerate(servers):
            print('++++++ server{} ++++'.format(i))
            txt = str(proc.stdout.read().decode())
            print(txt)
            txt = str(proc.stderr.read().decode())
            print(txt)
        print('+++++++++++++++++++')

    print('----- switch stats -----')
    tmp_names = (('server1', 'ex_vhost0'),('server2', 'ex_vhost2'),
            ('client1', 'ex_vhost1'), ('client2', 'ex_vhost3'),
            ('client3', 'ex_vhost4'))
    for name, port in tmp_names:
        print(name)
        cmd = 'show port {}'.format(port)
        p = bessctl_do(cmd, stdout=subprocess.PIPE)
        txt = p.stdout.decode()
        print(txt)

    # bessctl_do('command module client_qout0 get_pause_calls EmptyArg {}')
    FNULL = open(os.devnull, 'w') # pipe output to null

    # bessctl_do('show pipeline')
    count_client_vhost = 3
    count_server_vhost = 1
    for i in range(count_server_vhost):
        cmd = 'command module bkdrft_queue_out{} get_pause_calls EmptyArg {{}}'.format(i + count_client_vhost)
        bessctl_do(cmd, stdout=FNULL)
    bessctl_do('daemon stop', stdout=FNULL)

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
        default='bdvport', help='type of port used in pipeline configuration')

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

    if DIRECT_OUTPUT:
        print('=' * 32)
        print(' ' * 5, 'direct output is ON')
        print('=' * 32)

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
    _server_ips = ['10.10.1.3', '10.10.1.4']
    _client_ip = ['10.10.1.2', '10.10.1.3', '10.10.1.4']

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


    main()
