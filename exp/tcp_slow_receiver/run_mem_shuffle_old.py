#!/usr/bin/python3
"""
This is modified version of `run_exp` with the purpose of running
memcached and shuffle through BESS
"""

import sys
from time import sleep
import os
import argparse
import subprocess
import argparse
from pprint import pprint

sys.path.insert(0, '../')
from bkdrft_common import *
from tas_containers import *


cur_script_dir = os.path.dirname(os.path.abspath(__file__))
bessctl_dir = os.path.abspath(os.path.join(cur_script_dir,
    '../../code/bess-v1/bessctl'))
bessctl_bin = os.path.join(bessctl_dir, 'bessctl')

pipeline_config_temp = os.path.join(cur_script_dir,
    'pipeline_config_template.txt')
pipeline_config_file = os.path.join(cur_script_dir,
    'pipeline.bess')


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


def run_container(container):
    image_name = container['image']
    if image_name == 'tas_memcached':
        return spin_up_memcached(container)
    elif image_name == 'tas_shuffle':
        return spin_up_shuffle(container)
    else:
        msg = 'Container image name is expected to be tas_memcached or tas_shuffle got {}'
        msg = msg.format(image_name)
        raise ValueError(msg)


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
                book[name].append(float(value))
    return book


def str_format_pps(book):
    res = []
    res.append("=== pause call per sec ===")
    for name in book:
        res.append(name)
        for i, value in enumerate(book[name]):
            res.append('{} {}'.format(i, value))
    res.append("=== pause call per sec ===")
    txt = '\n'.join(res)
    return txt


def get_port_packets(port_name):
    p = bessctl_do('show port {}'.format(port_name), subprocess.PIPE)
    txt = p.stdout.decode()
    txt = txt.strip()
    lines = txt.split('\n')
    count_line = len(lines)
    res = { 'rx': { 'packet': -1, 'byte': -1, 'drop': -1},
            'tx': {'packet': -1, 'byte': -1, 'drop': -1}}
    if count_line < 6:
        return res

    raw = lines[2].split()
    res['rx']['packet'] = int(raw[2].replace(',',''))
    res['rx']['byte'] = int(raw[4].replace(',',''))
    raw = lines[3].split()
    res['rx']['drop'] = int(raw[1].replace(',',''))


    raw = lines[4].split()
    res['tx']['packet'] = int(raw[2].replace(',',''))
    res['tx']['byte'] = int(raw[4].replace(',',''))
    raw = lines[5].split()
    res['tx']['drop'] = int(raw[1].replace(',',''))
    return res


def delta_port_packets(before, after):
    res = {}
    for key, value in after.items():
        if isinstance(value, dict):
            res[key] = {}
            for key2, val2 in value.items():
                res[key][key2] = val2 - before[key][key2]
        elif isinstance(value, int):
            res[key] = value - before[key]
    return res


def mean(iterable):
    count = 0
    summ = 0
    for val in iterable:
        count += 1
        summ += val
    return summ / count


def get_cores(req):
    cpus = req
    count_instance = len(req)
    # number of available cores
    total_cores = 14  # TODO: this is hardcoded
    reserved_cores = 1  # possibly for BESS
    available_cores = total_cores - reserved_cores
    only_even = True  # for numa machinces (to place on one node)
    print('should place cores on even ids: ', only_even)
    # how many core each container have
    # count cpu is one of the arguments to this program

    cores =  []
    last_cpu_id = 0
    # this array helps to have better control on number of cores for each
    # instance. currently all of the containers have the same number of
    # cores
    for instance_num in range(count_instance):
        cpu_ids = []
        for cpu_num in range(cpus[instance_num]):
            cpu_id = last_cpu_id
            last_cpu_id += 1
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

    print('allocated cores:', ' | '.join(cores))
    return cores


def get_containers_config():
    if not os.path.exists('./tmp_vhost/'):
        os.mkdir('./tmp_vhost')
    socket_dir = os.path.abspath('./tmp_vhost')

    count_instance = 4
    cpus = [3, 3, 3, 4]  # request number of cpues for each instance
    cpus_share = [0.95, 0.71, 1.0, 1.0]
    cores = get_cores(cpus)

    containers = [
        # {
        #     # memcached
        #     'name': 'tas_server_1',
        #     'type': 'server',
        #     'image': 'tas_memcached',
        #     'cpu': cores[0],
        #     'socket': socket_dir + '/tas_server_1.sock',
        #     'ip': '10.10.0.1',
        #     'prefix': 'tas_server_1',
        #     'cpus': cpus[0] * cpus_share[0],
        #     'tas_cores': tas_cores,
        #     'tas_queues': count_queue,
        #     'cdq': int(cdq),
        #     'memory': 1024,
        #     'threads': 1,
        # },
        {
            # shuffle
            'name': 'tas_server_2',
            'type': 'server',
            'image': 'tas_shuffle',
            'cpu': cores[1],
            'socket': socket_dir + '/tas_server_2.sock',
            'ip': '10.10.0.2',
            'prefix': 'tas_server_2',
            'cpus': cpus[1] * cpus_share[1],
            'tas_cores': tas_cores,
            'tas_queues': count_queue,
            'cdq': int(cdq),
            'port': 5678,
        },
        # {
        #     # mutilate
        #     'name': 'tas_client_1',
        #     'type': 'client',
        #     'image': 'tas_memcached',
        #     'cpu': cores[2],
        #     'socket': socket_dir + '/tas_client_1.sock',
        #     'ip': '172.17.0.1',
        #     'prefix': 'tas_client_1',
        #     'cpus': cpus[2] * cpus_share[2],
        #     'tas_cores': tas_cores,
        #     'tas_queues': count_queue,
        #     'cdq': int(cdq),
        #     'dst_ip': '10.10.0.1',
        #     'duration': duration,
        #     'warmup_time': warmup_time,
        #     'wait_before_measure': 0,
        #     'threads': 1,
        #     'connections': mutilate_connections,
        # },
        {
            # shuffle
            'name': 'tas_client_2',
            'type': 'client',
            'image': 'tas_shuffle',
            'cpu': cores[3],
            'socket': socket_dir + '/tas_client_2.sock',
            'ip': '172.17.0.2',
            'prefix': 'tas_client_2',
            'cpus': cpus[3] * cpus_share[3],
            'tas_cores': tas_cores,
            'tas_queues': count_queue,
            'cdq': int(cdq),
            'dst_ip': '10.10.0.2',
            'server_port': 5678,
            'server_req_unit': shuffle_server_req_unit,
            'count_flow': shuffle_count_flow,
        },
        {
            # shuffle
            'name': 'tas_client_1',
            'type': 'client',
            'image': 'tas_shuffle',
            'cpu': cores[2],
            'socket': socket_dir + '/tas_client_1.sock',
            'ip': '172.17.0.1',
            'prefix': 'tas_client_1',
            'cpus': cpus[2] * cpus_share[2],
            'tas_cores': tas_cores,
            'tas_queues': count_queue,
            'cdq': int(cdq),
            'dst_ip': '10.10.0.2',
            'server_port': 5678,
            'server_req_unit': shuffle_server_req_unit,
            'count_flow': shuffle_count_flow,
        },
    ]
    return containers


def get_tas_results(containers, client_1_tas_logs):
    client_1_dst_count = len(containers[2]['ips'])
    lines = client_1_tas_logs.split('\n')
    count_line = len(lines)

    # skip the application related logs in the beginning
    count_skip_lines = 0
    while lines[count_skip_lines]:
        count_skip_lines += 1
    count_skip_lines += 1

    line_jump = (2 + client_1_dst_count)
    line_no_start = count_skip_lines + line_jump * (5 + warmup_time)
    line_no_end = count_skip_lines + line_jump * (5 + duration + warmup_time)
    tas_res = {'mbps': 0, 'pps': 0, 'latency@99.9': 0, 'latency@99.99': 0, 'drop': 0}
    tmp_count = 0

    # engine drop line
    if line_no_end < count_line:
        for line_no in range(line_no_start, line_no_end, line_jump):
            line = lines[line_no_end]
            raw = line.strip().split()
            drop = float(raw[1].split('=')[1])
            # print(line, drop)
            tas_res['drop'] += drop # accumulate drop per sec

            # app stats line
            line_no = line_no + 2
            line = lines[line_no]
            line = line.strip()
            # print(line)
            raw = line.split()
            tp = float(raw[1].split('=')[1])  # mbps
            tp_pkt = float(raw[3].split('=')[1])  #pps
            l1 = float(raw[13].split('=')[1]) # 99.9 us
            l2 = float(raw[15].split('=')[1]) # 99.9 us
            # tas_res['mbps'] = max(tp, tas_res['mbps'])
            # tas_res['pps'] = max(tp_pkt, tas_res['pps'])
            # tas_res['latency@99.9'] = max(l1, tas_res['latency@99.9'])
            # tas_res['latency@99.99'] = max(l2,tas_res['latency@99.99'])
            tas_res['mbps'] += tp
            tas_res['pps'] += tp_pkt
            tas_res['latency@99.9'] += l1
            tas_res['latency@99.99'] += l2
            tmp_count += 1

        tas_res['mbps'] = tas_res['mbps'] / tmp_count
        tas_res['pps'] = tas_res['pps'] / tmp_count
        tas_res['latency@99.9'] =  tas_res['latency@99.9'] / tmp_count
        tas_res['latency@99.99'] = tas_res['latency@99.99'] / tmp_count
    else:
        print('warning: TAS engine data not found, line number unexpected')
    return tas_res


def main():
    """
    About experiment:
           Memcached (small RPC) + Shuffle (large RPC)
    """
    # Get containers config
    containers = get_containers_config()

    # Kill anything running
    bessctl_do('daemon stop', stdout=subprocess.PIPE,
            stderr=subprocess.PIPE) # this may show an error it is nothing

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

    # Update pipeline configuration
    update_config()

    # Run a configuration (pipeline)
    file_path = pipeline_config_file
    ret = bessctl_do('daemon start -- run file {}'.format(file_path))

    if args.bessonly:
        print('BESS pipeline ready.')
        return

    print('==========================================')
    print('     TCP/Linux Socket App Performance Test')
    print('==========================================')

    # Spin up TAS
    print('running containers...')
    for c in containers:
        pprint(c)
        run_container(c)
        sleep(5)

    print('{} sec warm up...'.format(warmup_time))
    sleep(warmup_time)

    # Get result before
    before_res = get_port_packets('tas_server_1')

    try:
        # Wait for experiment duration to finish
        print('wait {} seconds...'.format(duration + 10))
        sleep(duration + 10)
    except KeyboardInterrupt:
        # catch Ctrl-C
        print('experiment termination process...')

    after_res = get_port_packets('tas_server_1')

    # client 1 tas logs
    client_1_container_name = containers[2]['name']
    p = subprocess.run('sudo docker logs {}'.format(client_1_container_name),
            shell=True, stdout=subprocess.PIPE)
    client_1_tas_logs = p.stdout.decode().strip()

    # craete log file
    output = open(output_log_file, 'w')
    output.close()

    print('gather TAS engine logs')
    logs = ['\n']
    for container in containers:
        name = container['name']
        p = subprocess.run('sudo docker logs {}'.format(name), shell=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        txt = p.stdout.decode()
        header = '=================\n{}\n================='.format(name)
        logs.append(header)
        logs.append(txt)

    # TODO: add switch port names to config and use a for loop
    logs.append('========= switch ==========')
    # BESS port stats
    p = bessctl_do('show port tas_server_1', subprocess.PIPE)
    txt = p.stdout.decode()
    logs.append(txt)
    # print('server1\n', txt)

    p = bessctl_do('show port tas_server_2', subprocess.PIPE)
    txt = p.stdout.decode()
    logs.append(txt)
    # print('server2\n', txt)

    p = bessctl_do('show port tas_client_1', subprocess.PIPE)
    txt = p.stdout.decode()
    logs.append(txt)
    # print('client1\n', txt)

    p = bessctl_do('show port tas_client_2', subprocess.PIPE)
    txt = p.stdout.decode()
    logs.append(txt)
    # print('client2\n', txt)

    # pause call per sec
    bessctl_do('command module bkdrft_queue_out0 get_pause_calls EmptyArg {}')
    bessctl_do('command module bkdrft_queue_out1 get_pause_calls EmptyArg {}')
    bessctl_do('command module bkdrft_queue_out2 get_pause_calls EmptyArg {}')
    bessctl_do('command module bkdrft_queue_out3 get_pause_calls EmptyArg {}')
    pps_val = get_pps_from_info_log()
    pps_log = str_format_pps(pps_val)
    # print(pps_log)

    logs.append(pps_log)
    logs.append('\n')

    # extract measures for client ==============
    # 1) get sw throughput and drop
    client_tp = delta_port_packets(before_res, after_res)
    # 2) get TAS throughput and latency
    try:
      tas_res = get_tas_results(containers, client_1_tas_logs)
    except Exception as e:
       # failed in parsing tas results
       print(e)
       tas_res = {}

    pps_avg = {}
    for key, vals in pps_val.items():
        avg = mean(vals[-duration:])
        pps_avg[key] = avg

    logs.append('\nclient 1 sw')
    logs.append(str(client_tp))
    logs.append('\nclient 1 tas')
    logs.append(str(tas_res))
    logs.append('\npps')
    logs.append(str(pps_avg))
    logs.append('\n')

    print('client 1 sw')
    print(str(client_tp))
    print('client 1 tas')
    print(str(tas_res))
    print('pps')
    print(str(pps_avg))
    # ==================


    # append switch log to client log file
    sw_log = '\n'.join(logs)
    with open(output_log_file, 'a') as log_file:
        log_file.write(sw_log)
    print('log copied to file: {}'.format(output_log_file))

    # Stop running containers
    print('stop and remove containers...')
    for container in reversed(containers):
        name = container.get('name')
        subprocess.run('sudo docker stop -t 0 {}'.format(name), shell=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        subprocess.run('sudo docker rm {}'.format(name), shell=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    # Stop BESS daemon
    bessctl_do('daemon stop')


if __name__ == '__main__':
    supported_apps = ('memcached', 'rpc', 'unidir', 'shuffle')
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
    parser.add_argument('--count_queue', type=int, required=False, default=1)
    parser.add_argument('--bessonly', action='store_true', required=False,
        default=False, help='only setup bess pipeline')
    parser.add_argument('--client_log', type=str, required=False,
        default='./client_log.txt', help='where to store log file')
    parser.add_argument('--duration', type=int, required=False, default=20)
    parser.add_argument('--warmup_time', type=int, required=False, default=60)

    args = parser.parse_args()
    slow_by = args.slow_by
    cdq = args.cdq
    pfq = args.pfq
    bp = args.bp
    lossless = args.buffering
    count_queue = args.count_queue
    duration = args.duration
    output_log_file = args.client_log
    warmup_time = args.warmup_time
    # tas should have only one core (not tested with more)
    tas_cores = 1  # how many cores are allocated to tas (fp-max-cores)

    mutilate_connections = 1
    shuffle_count_flow = 1  # shoudl be 1. shuffle app crashes otherwise
    shuffle_server_req_unit = 500 *  1024 * 1024 # 10GB


    # do parameter checks
    if cdq and count_queue < 2:
        print('command data queue needs at least two queues available')
        sys.exit(1)

    print('experiment parameters: ')
    print(('count_queue: {}    slow_by: {}   '
           'cdq: {}    pfq: {}    bp: {}'
          ).format(count_queue, slow_by, cdq, pfq, bp))

    main()

