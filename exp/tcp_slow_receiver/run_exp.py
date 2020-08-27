#!/usr/bin/python3
import sys
from time import sleep
import os
import argparse
import subprocess
import argparse

sys.path.insert(0, '../')
from bkdrft_common import *


cur_script_dir = os.path.dirname(os.path.abspath(__file__))
bessctl_dir = os.path.abspath(os.path.join(cur_script_dir,
    '../../code/bess-v1/bessctl'))
bessctl_bin = os.path.join(bessctl_dir, 'bessctl')

pipeline_config_temp = os.path.join(cur_script_dir,
    'pipeline_config_template.txt')
pipeline_config_file = os.path.join(cur_script_dir,
    'pipeline.bess')

tas_spinup_script = os.path.abspath(os.path.join(cur_script_dir,
    '../../code/apps/tas_container/spin_up_tas_container.sh'))
# tas_spinup_script = os.path.abspath(os.path.join(cur_script_dir,
#     '../../code/apps/tas_unidir/spin_up_tas_container.sh'))


def update_config():
    """
    Generate a BESS pipeline config file according to experiment settings
    and pipeline template file.
    """
    with open(pipeline_config_temp, 'r') as f:
        content = f.readlines()
    index = content.index('# == Insert here ==\n') + 1
    content.insert(index, "bp = {}\n".format(bp))
    content.insert(index, "cdq = {}\n".format(cdq))
    content.insert(index, "pfq = {}\n".format(pfq))
    content.insert(index, "lossless = {}\n".format(lossless))
    content.insert(index, "Q = {}\n".format(count_queue))

    with open(pipeline_config_file, 'w') as f:
        f.writelines(content)


def spin_up_tas(conf):
        """
        Spinup Tas Container

        note: path to spin_up_tas_container.sh is set in the global variable
        tas_spinup_script.
        
        conf: dict
        * name:
        * type: server, client
        * image: tas_container (it is the preferred image)
        * cpu: cpuset to use
        * socket: socket path
        * ip: current tas ip
        * prefix:
        * cpus: how much of cpu should be used (for slow receiver scenario)
        * port:
        * count_flow:
        * ips: e.g. [(ip, port), (ip, port), ...]
        * flow_duration
        * message_per_sec
        * tas_cores
        * tas_queues
        * cdq
        * message_size
        * flow_num_msg
        * count_threads
        """
        assert conf['type'] in ('client', 'server')

        temp = []
        for x in conf['ips']:
                x = map(str, x) 
                res = ':'.join(x)
                temp.append(res)
        _ips = ' '.join(temp)
        count_ips = len(conf['ips'])

        cmd = ('{tas_script} {name} {image} {cpu} {cpus:.2f} '
                '{socket} {ip} {tas_cores} {tas_queues} {prefix} {cdq} '
                '{type} {port} {count_flow} {count_ips} "{_ips}" {flow_duration} '
                '{message_per_sec} {message_size} {flow_num_msg} {count_threads}').format(
            tas_script=tas_spinup_script, count_ips=count_ips, _ips=_ips, **conf)
        print(cmd)
        return subprocess.run(cmd, shell=True)


def get_pps_from_info_log():
    """
    Go through /tmp/bessd.INFO file and find packet per second reports.
    Then organize and print them in stdout.
    """
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
    res = []
    res.append("=== pause call per sec ===")
    for name in book:
        res.append(name)
        for i, value in enumerate(book[name]):
            res.append('{} {}'.format(i, value))
    res.append("=== pause call per sec ===")
    txt = '\n'.join(res)
    return txt


def main():
    """
    About experiment:
        TCP Slow Receiver Test Using Tas (with an app).
        + BESS
        + BESS + bp
        + Per-Flow Input Queueing
        + PFIQ + bp
    """

    if not os.path.exists('./tmp_vhost/'):
        os.mkdir('./tmp_vhost')
    socket_dir = os.path.abspath('./tmp_vhost')

    # number of available cores
    total_cores = 14
    reserved_cores = 1  # possibly for BESS
    available_cores = total_cores - reserved_cores
    only_even = True  # for numa machinces (to place on one node)
    print('should place cores on even ids: ', only_even)
    # how many core each container have
    count_instance = 4  # how many container
    count_cpu = 3  # how many core to each container
    if count_cpu < 3:
        print('warning: each tas_container needs at least 3 cores to function properly')

    tas_cores = count_queue  # how many cores are allocated to tas

    cores =  []
    for instance_num in range(count_instance):
        cpu_ids = []
        for cpu_num in range(count_cpu):
            cpu_id = instance_num * count_cpu + cpu_num
            if cpu_id > available_cores:
                print('warning: number of cores was now enough some '
                'cores are assigned to multiple containers')
            cpu_id = cpu_id % available_cores
            cpu_id += reserved_cores
            if only_even:
                cpu_id = 2 * cpu_id
            cpu_ids.append(str(cpu_id))
        cpu_ids_str = ','.join(cpu_ids)
        cores.append(cpu_ids_str) 

    # how much cpu slow receiver have
    slow_rec_cpu = count_cpu * ((100 - slow_by) / 100)

    print(count_cpu, cores)

    # image_name = 'tas_unidir'
    image_name = 'tas_container'
    containers = [
        {
            'name': 'tas_server_1',
            'type': 'server',
            'image': image_name,
            'cpu': cores[0],
            'socket': socket_dir + '/tas_server_1.sock',
            'ip': '10.10.0.1',
            'prefix': 'tas_server_1',
            'cpus': count_cpu,
            'port': 1234,
            'count_flow': 10,
            'ips': [('10.0.0.2', 1234),],  # not used for server
            'flow_duration': 0,  # ms # not used for server
            'message_per_sec': -1,  # not used for server
            'tas_cores': tas_cores,
            'tas_queues': count_queue,
            'cdq': int(cdq),
            'message_size': 64,  # not used for server
            'flow_num_msg': 0,   # not used for server
            'count_threads': 1,
        },
        {
            'name': 'tas_server_2',
            'type': 'server',
            'image': image_name,
            'cpu': cores[1],
            'socket': socket_dir + '/tas_server_2.sock',
            'ip': '10.10.0.2',
            'prefix': 'tas_server_2',
            'cpus': slow_rec_cpu,
            'port': 5678,
            'count_flow': 10,
            'ips': [('10.0.0.2', 1234),],  # not used for server
            'flow_duration': 0,  # ms # not used for server
            'message_per_sec': -1,  # not used for server
            'tas_cores': tas_cores,
            'tas_queues': count_queue,
            'cdq': int(cdq),
            'message_size': 64,  # not used for server
            'flow_num_msg': 0,   # not used for server
            'count_threads': 1,
        },
        {
            'name': 'tas_client_1',
            'type': 'client',
            'image': image_name,
            'cpu': cores[2],
            'socket': socket_dir + '/tas_client_1.sock',
            'ip': '172.17.0.1',
            'prefix': 'tas_client_1',
            'cpus': count_cpu,
            'port': 7788,  # not used for client
            'count_flow': count_flow,
            'ips': [('10.10.0.1', 1234)],  # , ('10.10.0.2', 5678)
            'flow_duration': 0,
            'message_per_sec': -1,
            'tas_cores': tas_cores,
            'tas_queues': count_queue,
            'cdq': int(cdq),
            'message_size': 500,
            'flow_num_msg': 0,
            'count_threads': 1,
        },
        {
            'name': 'tas_client_2',
            'type': 'client',
            'image': image_name,
            'cpu': cores[3],
            'socket': socket_dir + '/tas_client_2.sock',
            'ip': '172.17.0.2',
            'prefix': 'tas_client_2',
            'cpus': count_cpu,
            'port': 7788,  # not used for client
            'count_flow': count_flow,
            'ips': [('10.10.0.2', 5678)],  # ('10.10.0.1', 1234), 
            'flow_duration': 0,
            'message_per_sec': -1,
            'tas_cores': tas_cores,
            'tas_queues': count_queue,
            'cdq': int(cdq),
            'message_size': 500,
            'flow_num_msg': 0,
            'count_threads': 1,
        },
    ]

    # Kill anything running
    bessctl_do('daemon stop') # this may show an error it is nothing

    # Stop and remove running containers
    print('remove containers from previous experiment')
    for c in reversed(containers):
        name = c.get('name')
        subprocess.run('sudo docker stop -t 0 {}'.format(name),
                shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        subprocess.run('sudo docker rm {}'.format(name),
                shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    # Run BESS daemon
    print('start BESS daemon')
    ret = bessctl_do('daemon start')
    if ret.returncode != 0:
        print('failed to start bess daemon', file=sys.stderr)
        return 1

    # Update configuration
    update_config()

    # Run a configuration (pipeline)
    file_path = pipeline_config_file
    ret = bessctl_do('daemon start -- run file {}'.format(file_path))

    if args.bessonly:
        print('BESS pipeline ready.')
        return 

    print('==============================')
    print('     TCP Performance Test')
    print('==============================')

    # Spin up TAS
    for c in containers:
        spin_up_tas(c)
        sleep(5)

    print('wait {} seconds...'.format(duration))
    sleep(duration)

    # Stop running containers
    print('stop containers...')
    for c in reversed(containers):
        name = c.get('name')
        subprocess.run('sudo docker stop -t 0 {}'.format(name), shell=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    # Collect logs and store in a file
    print('gather client log, through put and percentiles...')
    subprocess.run(
        'sudo docker cp tas_client_1:/tmp/log_drop_client.txt {}'.format(output_log_file),
        shell=True)
    subprocess.run('sudo chown $USER {}'.format(output_log_file), shell=True)

    print('gather TAS engine logs')
    logs = ['\n']
    for container in containers:
        name = container['name']
        p = subprocess.run('sudo docker logs {}'.format(name), shell=True,
                stdout=subprocess.PIPE)
        txt = p.stdout.decode()
        header = '=================\n{}\n================='.format(name)
        logs.append(header)
        logs.append(txt)


    logs.append('========= switch ==========')
    # BESS port stats
    p = bessctl_do('show port tas_server_1', subprocess.PIPE)
    txt = p.stdout.decode()
    logs.append(txt)
    print('server1\n', txt)

    p = bessctl_do('show port tas_server_2', subprocess.PIPE)
    txt = p.stdout.decode()
    logs.append(txt)
    print('server2\n', txt)

    p = bessctl_do('show port tas_client_1', subprocess.PIPE)
    txt = p.stdout.decode()
    logs.append(txt)
    print('client1\n', txt)

    p = bessctl_do('show port tas_client_2', subprocess.PIPE)
    txt = p.stdout.decode()
    logs.append(txt)
    print('client2\n', txt)

    # pause call per sec
    bessctl_do('command module bkdrft_queue_out0 get_pause_calls EmptyArg {}')
    bessctl_do('command module bkdrft_queue_out1 get_pause_calls EmptyArg {}')
    bessctl_do('command module bkdrft_queue_out2 get_pause_calls EmptyArg {}')
    bessctl_do('command module bkdrft_queue_out3 get_pause_calls EmptyArg {}')
    pps_log = get_pps_from_info_log()
    print(pps_log)

    logs.append(pps_log)
    logs.append('\n')

    # append swithc log to client log file
    sw_log = '\n'.join(logs)
    with open(output_log_file, 'a') as log_file:
        log_file.write(sw_log)
    print('log copied to file: {}'.format(output_log_file))

    # Stop running containers
    print('stop and remove containers...')
    for c in reversed(containers):
        name = c.get('name')
        subprocess.run('sudo docker stop {}'.format(name), shell=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        subprocess.run('sudo docker rm {}'.format(name), shell=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    # Stop BESS daemon
    bessctl_do('daemon stop')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--slow_by', type=float, required=False, default=0,
        help='slow down the component by percent [0, 100]')
    parser.add_argument('--cdq', action='store_true', required=False,
        default=False)
    parser.add_argument('--pfq', action='store_true', required=False,
        default=False)
    parser.add_argument('--bp', action='store_true', required=False,
        default=False)
    parser.add_argument('--buffering', action='store_true', required=False,
        default=False)
    parser.add_argument('--count_flow', type=int, required=False, default=1)
    parser.add_argument('--count_queue', type=int, required=False, default=1)
    parser.add_argument('--bessonly', action='store_true', required=False,
        default=False, help='only setup bess pipeline')
    parser.add_argument('--client_log', type=str, required=False,
        default='./client_log.txt', help='where to store log file')
    parser.add_argument('--duration', type=int, required=False, default=20)

    args = parser.parse_args()
    slow_by = args.slow_by
    cdq = args.cdq
    pfq = args.pfq
    bp = args.bp
    lossless = args.buffering
    count_flow = args.count_flow
    count_queue = args.count_queue
    duration = args.duration
    output_log_file = args.client_log

    print(('count_queue: {}    slow_by: {}   '
           'cdq: {}    pfq: {}    bp: {}    count_flow: {}\n'
          ).format(count_queue, slow_by, cdq, pfq, bp, count_flow))
    main()

