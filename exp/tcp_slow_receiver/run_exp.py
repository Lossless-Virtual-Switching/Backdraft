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
    # print(cmd)
    return subprocess.run(cmd, shell=True, stdout=subprocess.PIPE)


def spin_up_memcached(conf):
    cmd = ('{tas_script} {name} {image} {cpu} {cpus:.2f} '
            '{socket} {ip} {tas_cores} {tas_queues} {prefix} {cdq} '
            '{type} ').format(tas_script=tas_spinup_script, **conf)
    if conf['type'] == 'client':
        cmd = ('{cmd} {dst_ip} {duration} {warmup_time} {wait_before_measure} '
               '{threads} {connections}').format(cmd=cmd, **conf)
    elif conf['type'] == 'server':
        cmd = '{cmd} {memory} {threads}'.format(cmd=cmd, **conf) 
    else:
        raise Exception('Container miss configuration: '
                        'expecting type to be client or server')
    # print(cmd)
    return subprocess.run(cmd, shell=True, stdout=subprocess.PIPE)


def run_container(container):
    if app == 'memcached':
        return spin_up_memcached(container)
    else:
        return spin_up_tas(container)


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


def get_cores(count_instance):
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
    cpus = [count_cpu for i in range(count_instance)]
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

    print('count core for each container:', count_cpu)
    print('allocated cores:', ' | '.join(cores))
    return cores

def _get_rpc_unidir_containers(containers):
    app_params = [
        {   # server 1
            'port': 1234,
            'count_flow': 100,
            'ips': [('10.0.0.2', 1234),],  # not used for server
            'flow_duration': 0,  # ms # not used for server
            'message_per_sec': -1,  # not used for server
            'message_size': 64,  # not used for server
            'flow_num_msg': 0,   # not used for server
            'count_threads': 1,
        },
        {   # server 2
            'port': 5678,
            'count_flow': 100,
            'ips': [('10.0.0.2', 1234),],  # not used for server
            'flow_duration': 0,  # ms # not used for server
            'message_per_sec': -1,  # not used for server
            'message_size': 64,  # not used for server
            'flow_num_msg': 0,   # not used for server
            'count_threads': 1,
        },
        {   # client 1
            'port': 7788,  # not used for client
            'count_flow': 4,  # TODO: This has been set to const value for testing
            'ips': [('10.10.0.1', 1234)],  # , ('10.10.0.2', 5678)
            'flow_duration': 0,
            'message_per_sec': -1,
            'message_size': 500,
            'flow_num_msg': 0,
            'count_threads': 1,
        },
        {   # client 2
            'port': 7788,  # not used for client
            'count_flow': count_flow,
            'ips': [('10.10.0.2', 5678)],  # ('10.10.0.1', 1234), 
            'flow_duration': 0,
            'message_per_sec': -1,
            'message_size': 500,
            'flow_num_msg': 0,
            'count_threads': 1,
        },
    ]
    for container, params in zip(containers, app_params):
        container.update(params)
     

def _get_memcached_containers(containers):
    app_params = [
        {   # server 1
            'memory': 64,
            'threads': 1,
        },
        {   # server 2
            'memory': 64,   # not used for server
            'threads': 1,
        },
        {   # client 1
            'dst_ip': containers[0]['ip'],
            'duration': duration,
            'warmup_time': warmup_time,
            'wait_before_measure': 0,
            'threads': 1,
            'connections': count_flow,
        },
        {   # client 2
            'dst_ip': containers[1]['ip'],
            'duration': duration,
            'warmup_time': warmup_time,
            'wait_before_measure': 0,
            'threads': 1,
            'connections': count_flow,
        },
    ]
    for container, params in zip(containers, app_params):
        container.update(params)


def get_containers_config():
    if not os.path.exists('./tmp_vhost/'):
        os.mkdir('./tmp_vhost')
    socket_dir = os.path.abspath('./tmp_vhost')

    count_instance = 4
    cores = get_cores(count_instance)
    # how much cpu slow receiver have
    slow_rec_cpu = count_cpu * ((100 - slow_by) / 100)

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
            'tas_cores': tas_cores,
            'tas_queues': count_queue,
            'cdq': int(cdq),
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
            'tas_cores': tas_cores,
            'tas_queues': count_queue,
            'cdq': int(cdq),
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
            'tas_cores': tas_cores,
            'tas_queues': count_queue,
            'cdq': int(cdq),
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
            'tas_cores': tas_cores,
            'tas_queues': count_queue,
            'cdq': int(cdq),
        },
    ]
    if app == 'memcached':
        _get_memcached_containers(containers)
    else:
        _get_rpc_unidir_containers(containers)
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
        TCP Slow Receiver Test Using Tas (with an app).
        + BESS
        + BESS + bp
        + Per-Flow Input Queueing
        + PFIQ + bp
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

    # Update configuration
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
        run_container(c)
        sleep(5)

    print('{} sec warm up...'.format(warmup_time))
    sleep(warmup_time)

    # Get result before
    before_res = get_port_packets('tas_server_1')

    try:
        # Wait for experiment duration to finish
        print('wait {} seconds...'.format(duration))
        sleep(duration)
    except:
        # catch Ctrl-C
        print('experiment termination process...')

    after_res = get_port_packets('tas_server_1')

    # client 1 tas logs
    client_1_container_name = containers[2]['name']
    p = subprocess.run('sudo docker logs {}'.format(client_1_container_name),
            shell=True, stdout=subprocess.PIPE)
    client_1_tas_logs = p.stdout.decode().strip()

    if app == 'rpc':
        # collect logs and store in a file
        print('gather client log, through put and percentiles...')
        subprocess.run(
            'sudo docker cp tas_client_1:/tmp/log_drop_client.txt {}'.format(output_log_file),
            shell=True, stdout=subprocess.PIPE)
        subprocess.run('sudo chown $USER {}'.format(output_log_file), shell=True, stdout=subprocess.PIPE)
    else:
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
    if cdq:
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


    # append swithc log to client log file
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
    supported_apps = ('memcached', 'rpc', 'unidir')
    parser = argparse.ArgumentParser()
    parser.add_argument('app', choices=supported_apps)
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
    parser.add_argument('--warmup_time', type=int, required=False, default=60)

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
    app = args.app
    warmup_time = args.warmup_time
    # tas should have only one core (not tested with more)
    tas_cores = 1  # how many cores are allocated to tas (fp-max-cores)

    # select the app
    if app == 'rpc':
        # micro rpc modified
        tas_spinup_script = os.path.abspath(os.path.join(cur_script_dir,
            '../../code/apps/tas_container/spin_up_tas_container.sh'))
        image_name = 'tas_container'
    elif app == 'unidir':
        # unidir
        tas_spinup_script = os.path.abspath(os.path.join(cur_script_dir,
            '../../code/apps/tas_unidir/spin_up_tas_container.sh'))
        image_name = 'tas_unidir'
    elif app == 'memcached':
        # memcached
        tas_spinup_script = os.path.abspath(os.path.join(cur_script_dir,
            '../../code/apps/tas_memcached/spin_up_tas_container.sh'))
        image_name = 'tas_memcached'
    else:
        print('app name unexpected: {}'.format(app), file=sys.stderr)
        sys.exit(1)


    # do parameter checks
    if count_cpu < 3:
        print('warning: each tas_container needs at least 3 cores to function properly')

    if cdq and count_queue < 2:
        print('command data queue needs at least two queues available')
        sys.exit(1)


    print('experiment parameters: ')
    print('running app: {}'.format(app))
    print(('count_queue: {}    slow_by: {}   '
           'cdq: {}    pfq: {}    bp: {}    count_flow: {}'
          ).format(count_queue, slow_by, cdq, pfq, bp, count_flow))
    main()

