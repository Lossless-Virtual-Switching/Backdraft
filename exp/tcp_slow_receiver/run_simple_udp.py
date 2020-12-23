#!/usr/bin/python3
import os
import sys
import subprocess
import argparse
from time import sleep
sys.path.insert(0, '../')
from bkdrft_common import *
from tas_containers import *


cur_script_dir = os.path.dirname(os.path.abspath(__file__))
pipeline_config_temp = os.path.join(cur_script_dir,
    'simple_pipeline.txt')
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



def main():
    # Kill anything running
    bessctl_do('daemon stop', stdout=subprocess.PIPE,
            stderr=subprocess.PIPE) # this may show an error it is nothing

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

    print('===============================================')
    print('     UDP/DPDK Socket App Performance Test')
    print('===============================================')


    # Run applications
    app_configs = [
            {
                'type': 'server',
                'cpu': 4,
                'prefix': 'test_udp_server_prefix',
                'vdev': 'virtio_user0,path=tmp_vhost/tas_server_1.sock,queues='+str(count_queue),
                'ip': '10.10.0.1',
                'count_queue': count_queue,
                'sysmod': 'bkdrft' if cdq else 'bess',
                'delay': 0,
                },
            {
                'type': 'client',
                'cpu': '(8,10)',
                'prefix': 'test_udp_client_prefix',
                'vdev': 'virtio_user1,path=tmp_vhost/tas_client_1.sock,queues='+str(count_queue),
                'ip': '10.10.0.1',
                'count_queue': count_queue,
                'sysmod': 'bkdrft' if cdq else 'bess',
                'ips': ['10.10.0.1', ],
                'count_flow': count_flow,
                'duration': duration,
                'port': 1234,
                'delay': 0,
                'rate': 1000000,
                },
            ]
    procs = []
    for conf in app_configs:
        p = run_udp_app(conf)
        procs.append(p)
    client_p = procs[1]

    # Get result before
    before_res = get_port_packets('tas_server_1')

    client_p.wait()


    after_res = get_port_packets('tas_server_1')


    # Create log file
    output = open(output_log_file, 'w')
    output.close()

    logs = []

    txt = client_p.stdout.read().decode()
    logs.append(txt)

    logs.append('========= switch ==========')
    # BESS port stats
    p = bessctl_do('show port tas_server_1', subprocess.PIPE)
    txt = p.stdout.decode()
    logs.append(txt)

    p = bessctl_do('show port tas_client_1', subprocess.PIPE)
    txt = p.stdout.decode()
    logs.append(txt)

    # pause call per sec
    print('before commands')
    bessctl_do('command module bkdrft_queue_out0 get_pause_calls EmptyArg {}')
    bessctl_do('command module bkdrft_queue_out1 get_pause_calls EmptyArg {}')
    print('after commands')

    pps_val = get_pps_from_info_log()
    pps_log = str_format_pps(pps_val)
    # print(pps_log)

    logs.append(pps_log)
    logs.append('\n')

    # append swithc log to client log file
    sw_log = '\n'.join(logs)
    with open(output_log_file, 'a') as log_file:
        log_file.write(sw_log)
    print('log copied to file: {}'.format(output_log_file))

    subprocess.run('sudo pkill udp_app', shell=True)
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
    parser.add_argument('--count_cpu', type=int, required=False, default=3,
            help='how many cores each container should have')

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
    count_cpu = args.count_cpu
    # tas should have only one core (not tested with more)
    tas_cores = 1  # how many cores are allocated to tas (fp-max-cores)
    dummy_proc_cost = 500  # cycles per pkt
    dummy_target_ip_list = []

    # do parameter checks
    if count_cpu < 3:
        print('warning: each tas_container needs at least 3 cores to function properly')

    if cdq and count_queue < 2:
        print('command data queue needs at least two queues available')
        sys.exit(1)

    print('experiment parameters: ')
    print(('count_queue: {}    slow_by: {}   '
        'cdq: {}    pfq: {}    bp: {}    count_flow: {}'
        ).format(count_queue, slow_by, cdq, pfq, bp, count_flow))

    main()

