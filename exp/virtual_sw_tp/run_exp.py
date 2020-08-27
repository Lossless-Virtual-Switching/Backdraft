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

sys.path.insert(0, '../')
from bkdrft_common import *


cur_script_dir = os.path.dirname(os.path.abspath(__file__))

pipeline_config_file = os.path.join(cur_script_dir,
    'pipeline.bess')

udp_app_dir = os.path.join(cur_script_dir, '../../code/apps/udp_client_server')
udp_app_bin = os.path.join(udp_app_dir, 'build/') # udp_app


def _stop_everything():
    try:
      bessctl_do('daemon stop') # this may show an error it is nothing
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
            '{source_ip} {count_queue} {type} client {count_dst} {ips} {count_flow}').format(**conf)
    return subprocess.Popen(cmd, shell=True, cwd=udp_app_bin)


def run_server(conf):
    cmd = ('sudo ./udp_app -l {cpuset} --no-pci --file-prefix={prefix} --vdev="{vdev}" -- '
            '{source_ip} {count_queue} {type} server {delay}').format(**conf)
    return subprocess.Popen(cmd, shell=True, cwd=udp_app_bin)


def get_pfc_results():
    log = subprocess.check_output('ethtool -S eno50 | grep prio3_pause', shell=True)
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
      pipeline_conf['count_core'] = count_core
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

    server_conf = {
            'cpuset': cpuset,
            'prefix': 'server',
            'vdev': 'virtio_user0,path=/tmp/vhost_0.sock,queues={}'.format(count_queue),
            'count_queue': count_queue,
            'type': app_mode,
            'source_ip': '10.0.0.1',
            'delay': server_batch_delay,
            }
    client_conf = {
            'cpuset': cpuset,
            'prefix': 'client',
            'vdev': 'virtio_user0,path=/tmp/vhost_0.sock,queues={}'.format(count_queue),
            'count_queue': count_queue,
            'type': app_mode,
            'source_ip': '10.0.0.2', 
            'count_dst': 1,
            'ips': '10.0.0.1',
            'count_flow': 1,
            }


    # Kill anything running
    _stop_everything()
 
    # Setup BESS config
    file_path = pipeline_config_file
    ret = bessctl_do('daemon start -- run file {}'.format(file_path))

    # pfc_stats_before = get_pfc_results()

    clients = []
    server_dict = {}
    last_core_id = count_core
    for i in range(0, 2 * count_core, 2):
        vdev = 'virtio_user{0},path=/tmp/vhost_{0}.sock,queues={1}'.format(i, count_queue)
        server_conf['vdev'] = vdev
        server_conf['cpuset'] = last_core_id
        server_conf['prefix'] = 'server_{}'.format(i)
        # last_core_id += 1
        # server = run_server(server_conf)

        j = i + 1
        vdev = 'virtio_user{0},path=/tmp/vhost_{0}.sock,queues={1}'.format(j, count_queue)
        client_conf['vdev'] = vdev
        client_conf['cpuset'] = last_core_id
        client_conf['prefix'] = 'client_{}'.format(i)
        last_core_id += 1
        client = run_client(client_conf)

        # server_dict[i] = server
        clients.append(client)

    for i, client in enumerate(clients):
        client.wait()
        # server_dict[i * 2].kill()

    # experiment finished
    # ret = bessctl_do('show port', subprocess.PIPE)
    # log = ret.stdout.decode()
    # print(log)

    sum_pkts = 0
    sum_bytes = 0
    for i in range(1, 2 * count_core + 1, 2):
      ret = bessctl_do('show port port_{}'.format(i), subprocess.PIPE)
      log = ret.stdout.decode()
      log = log.strip()
      print(log)
      lines = log.split('\n')

      line = lines[2].strip()
      raw = line.split()
      pkts = float(raw[2].replace(',', ''))
      byte = float(raw[4].replace(',', ''))
      sum_pkts += pkts
      sum_bytes += byte

      # line = lines[4].strip()
      # raw = line.split()
      # pkts = float(raw[2].replace(',', ''))
      # byte = float(raw[4].replace(',', ''))
      # sum_pkts += pkts
      # sum_bytes += byte

    print ('throughput: {:2f} (Mpps) {:2f} (Gbps)'.format(sum_pkts / 40 / 1e6, sum_bytes * 8 / 40 / 1e9))

    
    _stop_everything()

    # pfc_stats_after = get_pfc_results()
    # pfc_stats = get_delta_pfc(pfc_stats_before, pfc_stats_after) 
    # if args.output == None:
    #     pprint(pfc_stats)
    # else:
    #     with open(arg.output, 'w') as f:
    #         f.write(str(pfc_stats))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    # parser.add_argument('app', choices=app_modes,
    #         help='run a client or a server')
    
    parser.add_argument('count_core', type=int)
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
    args = parser.parse_args()

    # source_ip = args.source_ip
    count_core = args.count_core

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

    main()

