#!/usr/bin/python3
"""
Backdraft Overlay Experiment Script
"""
import os
import sys
from time import sleep
import argparse
import subprocess
import json
from pprint import pprint

sys.path.insert(0, '../../')
from bkdrft_common import *


cur_script_dir = os.path.dirname(os.path.abspath(__file__))

pipeline_config_file = os.path.join(cur_script_dir,
    'pipeline.bess')

udp_app_dir = os.path.join(cur_script_dir, '../../../code/apps/udp_client_server')
udp_app_bin = os.path.join(udp_app_dir, 'build/') # udp_app


def _stop_everything():
    try:
      bessctl_do('daemon stop', stderr=subprocess.PIPE)
    except:
      # bess was not running
      pass

    subprocess.run('sudo pkill udp_app', shell=True)


def run_docker_container(container):
    cmd = 'sudo docker run --cpuset-cpus={cpuset} --cpus={cpu_share} -d --network none --name {name} {image} {args}'
    cmd = cmd.format(**container)
    print(cmd)
    subprocess.run(cmd, shell=True)


def run_client(conf):
    cmd = ('sudo ./udp_app -l {cpuset} --no-pci --file-prefix={prefix} --vdev="{vdev}" -- '
            '{source_ip} {count_queue} {type} client {count_dst} {ips} '
            '{count_flow} {duration}').format(**conf)
    return subprocess.check_call(cmd, shell=True, cwd=udp_app_bin)


def run_server(conf):
    cmd = ('sudo ./udp_app -l {cpuset} --no-pci --file-prefix={prefix} --vdev="{vdev}" -- '
            '{source_ip} {count_queue} {type} server {delay}').format(**conf)
    return subprocess.check_call(cmd, shell=True, cwd=udp_app_bin)


def get_pfc_results():
    interface = 'enp65s0f0'
    log = subprocess.check_output('ethtool -S {} | grep prio3_pause'.format(interface), shell=True)
    log = log.decode()
    log = log.strip()
    lines = log.split('\n')
    res = {}
    for line in lines:
        line = line.strip()
        key, value = line.split(':')
        res[key] = int(value)
    return res


def get_delta_pfc(before, after):
    res = {}
    for key, value in after.items():
        res[key] = value - before[key]
    return res


def main():
    # Write pipeline config file
    if os.path.exists('.pipeline_config.json'):
        try:
            pipeline_conf = json.load(open('.pipeline_config.json'))
        except:
            pipeline_conf = {}
    else:
        pipeline_conf = {}

    with open('.pipeline_config.json', 'w') as f:
      pipeline_conf['count_queue'] = count_queue
      pipeline_conf['queue_size'] = queue_size
      pipeline_conf['cdq'] = cdq
      pipeline_conf['pfq'] = pfq
      pipeline_conf['bp'] = bp
      pipeline_conf['buffering'] = buffering
      pipeline_conf['overlay'] = overlay
      txt = json.dumps(pipeline_conf)
      f.write(txt)

    if cdq:
        app_mode = 'bkdrft'
    else:
        app_mode = 'bess'
    print(app_mode)

    server_conf = {
            'cpuset': cpuset,
            'prefix': 'server',
            'vdev': 'virtio_user0,path=/tmp/vhost_0.sock,queues={}'.format(count_queue),
            'count_queue': count_queue,
            'type': app_mode,
            'source_ip': source_ip,
            'delay': server_batch_delay,
            }
    client_conf = {
            'cpuset': cpuset,
            'prefix': 'client',
            'vdev': 'virtio_user0,path=/tmp/vhost_0.sock,queues={}'.format(count_queue),
            'count_queue': count_queue,
            'type': app_mode,
            'source_ip': source_ip, 
            'count_dst': count_dst_ip,
            'ips': dst_ips,
            'count_flow': count_flow,
            'duration': duration,
            }


    # Kill anything running
    _stop_everything()
 
    # Setup BESS config
    file_path = pipeline_config_file
    ret = bessctl_do('daemon start -- run file {}'.format(file_path))
    if ret.returncode != 0:
        print('error: bess pipeline failed')
        sys.exit(1)

    if args.bessonly:
        print('bess only mode: only setup bess pipeline')
        sys.exit(0)

    pfc_stats_before = get_pfc_results()

    try: 
        if args.app == 'client':
            run_client(client_conf)
        elif args.app == 'server':
            res = run_server(server_conf)
        else:
            print('app should be client or server')
    except:
        # add this try except for catching CTR-C for the server
        pass

    # experiment is finished, gather data
    pfc_stats_after = get_pfc_results()

    print('switch stats')
    p = bessctl_do('show port', subprocess.PIPE)
    txt = p.stdout.decode()
    print(txt)
    print('')

    print('overlay packet per second average:')
    for i in range(2):
        module_name = 'bkdrft_queue_out{0}'.format(i)
        cmd =  'command module {0} get_overlay_tp EmptyArg {{}}'
        cmd = cmd.format(module_name)
        res = bessctl_do(cmd, stdout=subprocess.PIPE)
        log = res.stdout.decode()
        log = log.replace('response: ', '').replace('throughput: ','').strip()
        if not log:
            print('module:', module_name, 'avg: 0')
            continue
        overlay_tp = list(map(float, log.split('\n')))
        overlay_tp2 = overlay_tp[-duration:]
        avg = sum(overlay_tp2) / duration
        print('module:', module_name, 'avg: {:.2f}'.format(avg))
        print(overlay_tp)
    print('')

    print ('overlay Rx per queue:')
    for i in range(2):
        module_name = 'bkdrft_queue_inc{0}'.format(i)
        cmd =  'command module {0} get_overlay_stats EmptyArg {{}}'
        cmd = cmd.format(module_name)
        res = bessctl_do(cmd, stdout=subprocess.PIPE)
        log = res.stdout.decode()
        log = log.replace('response: ', '')
        log_lines = log.split('\n')
        pkt_logs = filter(lambda x: 'pkts' in x, log_lines)
        duration_logs = filter(lambda x: 'duration' in x, log_lines)
        pkt_logs = map(lambda x: int(x.strip().split()[1]), pkt_logs)
        duration_logs = map(lambda x: int(x.strip().split()[1]), duration_logs)
        print('module:', module_name)
        for q, (pkt, ov_duration) in enumerate(zip(pkt_logs, duration_logs)):
            print('q:', q, 'pkts', pkt, 'duration (ns)', ov_duration)
        print('------------------')
    print('')
 

    # if bp:
    #     print('pause call per second')
    #     bessctl_do('command module bkdrft_queue_out0 get_pause_calls EmptyArg {}')
    #     bessctl_do('command module bkdrft_queue_out1 get_pause_calls EmptyArg {}')
    #     pps_val = get_pps_from_info_log()
    #     # pprint(pps_val)
    #     # pps_log = str_format_pps(pps_val)
        

    _stop_everything()

    pfc_stats = get_delta_pfc(pfc_stats_before, pfc_stats_after) 
    if args.output == None:
        print('pfc stats:')
        pprint(pfc_stats)
    else:
        with open(arg.output, 'w') as f:
            f.write(str(pfc_stats))


if __name__ == '__main__':
    app_modes = ('client', 'server')
    parser = argparse.ArgumentParser()
    parser.add_argument('app', choices=app_modes,
            help='run a client or a server')
    parser.add_argument('source_ip', help='current node ip address')
    parser.add_argument('--server_batch_delay', type=int, default=0,
            help='amount of work server does for each batch of packets (in us)')
    parser.add_argument('--count_queue', type=int, default=1,
            help='number of queues available to the app')
    parser.add_argument('--queue_size', type=int, default=64,
            help='size of each queue')
    parser.add_argument('--cdq', action='store_true',
            help='use command queue and datat queue')
    parser.add_argument('--pfq', action='store_true',
            help='enable per-flow queueing')
    parser.add_argument('--bp', action='store_true',
            help='enable backpressure')
    parser.add_argument('--buffering', action='store_true',
            help='use buffering mechanism')
    parser.add_argument('--overlay', action='store_true',
            help='enable overlay protocol')
    parser.add_argument('--cpuset', default='5',
            help='allocated cpus for the application')
    parser.add_argument('--output', default=None, required=False,
            help="results will be writen in the given file")
    parser.add_argument('--dst_ips', default='192.168.1.4 192.168.1.3',
        help='client destination ip adresses (e.g. "192.168.1.3 192.168.1.4")')
    parser.add_argument('--duration', type=int, default=20,
            help='experiment duration')
    parser.add_argument('--bessonly', action='store_true', default=False,
            help='only setup bess pipeline')
    args = parser.parse_args()

    source_ip = args.source_ip
    count_queue = args.count_queue
    queue_size = args.queue_size
    cdq = args.cdq
    pfq = args.pfq
    bp = args.bp
    buffering = args.buffering
    overlay = args.overlay
    cpuset = args.cpuset
    server_batch_delay = args.server_batch_delay
    dst_ips = args.dst_ips
    count_dst_ip = len(dst_ips.strip().split())
    duration = args.duration
    count_flow = 1

    if cdq and count_queue < 2:
        print('in cdq mode there should be at least 2 queueus')
        sys.exit(1)

    if overlay and not cdq:
        print('overlay needs the system to support cdq')
        sys.exit(1)

    if overlay and not buffering:
        print('warning: overlay is enabled but buffering is not!')

    try:
        main()
    except Exception as e:
        print('experiment failed with an exception')
        print(e)

