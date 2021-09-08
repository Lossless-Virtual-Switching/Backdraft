#!/usr/bin/python3
"""
Backdraft Homa Experiment Script
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
    # '../homa/pipeline_pfq.bess')
    # '../homa/pipeline.bess')
    # 'pipeline_bd.bess')
    # 'pipeline_incast_bd.bess')
    # 'pipeline_incast_pfq.bess')
    'pipeline_default.bess')
    # 'pipeline_default_1_server.bess')
    # 'pipeline_incast_bess_bp.bess')

homa_base = os.path.join(cur_script_dir, '../../code/Homa')
homa_app_bin = os.path.join(homa_base, 'build/test/dpdk_test')


def _stop_everything():
    try:
      bessctl_do('daemon stop') # this may show an error it is nothing
    except:
      # bess was not running pass
      pass

    subprocess.run('sudo pkill dpdk_test', shell=True)
    subprocess.run('sudo pkill dpdk_client_sys', shell=True)
    subprocess.run('sudo pkill dpdk_server_sys --signal 2', shell=True)


def run_system_perf_client(conf):
    client_bin = './build/test/dpdk_openloop_test'
    victim = ''
    req_count = 1000000
    if 'victim' in conf and conf['victim']:
        victim = '--victim'
        req_count = 1000000
    # cmd = ("sudo perf stat -e task-clock,cycles,instructions,cache-references,cache-misses {} 1000000 -v --delay={} --id={} "
    # cmd = ("sudo perf record -ag {} 1000000 -v --delay={} --id={} "
    cmd = ("sudo {} {} -vvv --delay={} --id={} "
    "--barriers={} --vhost-port --iface='--vdev=virtio_user0,path={}' "
    "--dpdk-extra=--no-pci --size={} --dpdk-extra='--file-prefix=mg-{}' "
    "--dpdk-extra='-l' --dpdk-extra='{}' --vhost-port-ip={} "
    "--vhost-port-mac={} {} ").format(client_bin, req_count, conf["delay"], conf["id"],
            conf["barriers"], conf["path"], conf["tx_pkt_length"], conf["ip"], conf["cpuset"],
            conf["ip"], conf["mac"], victim)

    print("client {}".format(cmd))

    return subprocess.Popen(cmd, shell=True, cwd=homa_base) # ,
            # stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding='utf8')


def run_system_perf_server(conf):
    server_bin = './build/test/dpdk_server_system_test'
    cmd = ("sudo {} 100 --server=1 -v "
            "--vhost-port --iface='--vdev=virtio_user0,path={}' "
            "--dpdk-extra=--no-pci --dpdk-extra='-l' --dpdk-extra='{}' "
            "--dpdk-extra='--file-prefix=mg-{}' --vhost-port-ip={} "
            "--vhost-port-mac={}").format(server_bin, conf['path'],
                    conf['cpuset'], conf['cpuset'], conf["ip"], conf["mac"])

    if conf["slow_down"]:
        cmd = "sudo cpulimit -l {} -- {} 100 \
        --server=1 -v --vhost-port --iface='--vdev=virtio_user0,path={}' \
        --dpdk-extra=--no-pci --dpdk-extra='-l' --dpdk-extra='{}' \
        --dpdk-extra='--file-prefix=mg-{}' --vhost-port-ip={} \
        --vhost-port-mac={}".format(conf["slow_down"], server_bin, conf['path'],
                conf['cpuset'], conf['cpuset'], conf["ip"], conf["mac"])

    print("server {}".format(cmd))

    return subprocess.Popen(cmd, shell=True, cwd=homa_base,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding='utf8')


def get_pfc_results():
    log = subprocess.check_output('ethtool -S eno50 | grep prio3_pause',
            shell=True)
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
    # if os.path.exists('.pipeline_config.json'):
    if os.path.exists('/tmp/.pipeline_config.json'):
        try:
            pipeline_conf = json.load(open('/tmp/.pipeline_config.json'))
        except:
            pipeline_conf = {}
    else:
        pipeline_conf = {}

    with open('/tmp/.pipeline_config.json', 'w') as f:
      pipeline_conf['count_core'] = count_core
      pipeline_conf['count_queue'] = count_queue
      pipeline_conf['queue_size'] = queue_size
      pipeline_conf['pci_address'] = pci
      pipeline_conf['vhost_port_count'] = vhost_port_count
      pipeline_conf['server'] = True if mode == 'server' else False
      pipeline_conf['drop_prob'] = drop_probability
      txt = json.dumps(pipeline_conf)
      f.write(txt)

    # Setup BESS config
    file_path = pipeline_config_file
    ret = bessctl_do('daemon start -- run file {}'.format(file_path))
    print(ret.returncode)
    if ret.returncode != 0:
      print("bess has issues")
      return 1

    server_process = []
    client_process = []

    ret = bessctl_do('show pipeline', subprocess.PIPE)
    print(ret.stdout.decode().strip())

    # exit(0)

    if mode == "server": 
        # run_server(server_conf)
        for i in range(vhost_port_count):
            server_conf = {
                'cpuset': i + 10,
                # 'prefix': 'server',
                # 'path': '/tmp/vhost_{}.sock,queues={}'.format(i, count_queue),
                'path': '/tmp/vhost_{}.sock,queues={}'.format(i, 1),
                # 'count_queue': count_queue,
                # 'type': app_mode,
                'mac': '9c:dc:71:5b:22:a1',
                'ip': '192.168.1.1',
                'slow_down': slow_down,
                'tx_pkt_length': tx_size
            }
            sp = run_system_perf_server(server_conf)
            server_process.append(sp)

        ############## SLEEEEEEEEEEEEEEEP
        sleep(time + 10) # This means you only have 10 seconds to run the server.
    else:
        for i in range(vhost_port_count):
            # print('ip ' + "192.168.1.{}".format(i + 1))
            client_conf = {
              'cpuset': f'{i*4+6}-{i*4+9}', # it is just random
              # 'prefix': 'client',
              # 'path': '/tmp/vhost_{}.sock,queues={}'.format(i, count_queue),
              'path': '/tmp/vhost_{}.sock,queues={}'.format(i, 1),
              'slow_down': slow_down,
              'tx_pkt_length': tx_size,
              'file_prefix': i,
              'ip': "192.168.1.{}".format(i + 3),
              'mac': "9c:dc:71:5e:0f:e1",
              'barriers': vhost_port_count,
              'id': i,
              # 'delay': 100 if i%2==1 else delay 
              'delay': delay,
              'victim': i == 0,
            }

            cp = run_system_perf_client(client_conf)
            sleep(0.5)
            client_process.append(cp)


        # client_bin = './build/test/dpdk_openloop_test'
        # pid = client_process[0].pid
        # os.execv('/usr/bin/gdb', ['--pid', str(pid), client_bin])

        for i in range(vhost_port_count):
            client_process[i].wait()

    sum_pkts = 0
    sum_bytes = 0

    # ret = bessctl_do('show pipeline', subprocess.PIPE)
    # print(ret.stdout.decode())
    ret = bessctl_do('show port', subprocess.PIPE)
    print(ret.stdout.decode().strip())

    ret = bessctl_do('command module measure_pps0 get_summary EmptyArg',
            subprocess.PIPE, subprocess.PIPE)
    if ret.returncode != 0:
        print("command measure pps failed")
    else:
        stdout = ret.stdout.decode()
        print('STDOUT:{}'.format(stdout))

    for i in range(vhost_port_count):

      ret = bessctl_do('show port port_{}'.format(i), subprocess.PIPE)
      # if(ret != 0):
      #   print("failed to run: " +'show port port_{}'.format(i))
      #   continue
      log = ret.stdout.decode()
      log = log.strip()
      # print(log)
      lines = log.split('\n')

      line = lines[2].strip()
      raw = line.split()
      pkts = float(raw[2].replace(',', ''))
      byte = float(raw[4].replace(',', ''))
      sum_pkts += pkts
      sum_bytes += byte

      line = lines[4].strip()
      raw = line.split()
      pkts = float(raw[2].replace(',', ''))
      byte = float(raw[4].replace(',', ''))
      sum_pkts += pkts
      sum_bytes += byte

    ret = bessctl_do('show tc', subprocess.PIPE)
    log = ret.stdout.decode()
    print(log)

    for i in range(10):
      # pause frame
      name = 'bpq_inc{}'.format(i)
      print(name)
      ret = bessctl_do('command module {} get_summary EmptyArg {{}}'.format(name), subprocess.PIPE, subprocess.PIPE)
      log = ret.stdout.decode()
      print(log)
      name = 'bpq_out{}'.format(i)
      print(name)
      ret = bessctl_do('command module {} get_summary EmptyArg {{}}'.format(name), subprocess.PIPE, subprocess.PIPE)
      log = ret.stdout.decode()
      print(log)

    # I don't have the time of experiment.
    print ('throughput: {:2f} (Mpps) {:2f} (Gbps)'.format(sum_pkts / 40 / 1e6, sum_bytes * 8 / 40 / 1e9))

    if client_process:
        for proc in client_process:
            stdout = proc.communicate()[0]
            print('STDOUT:{}'.format(stdout))

    sleep(1)
    _stop_everything()

    if server_process:
        print(server_process)
        for proc in server_process:
            stdout = proc.communicate()[0]
            print('STDOUT:{}'.format(stdout))

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
    parser.add_argument('count_vhost_port', type=int, help="Number of vhost ports")
    parser.add_argument('mode', type=str, help="server OR client")

    parser.add_argument('--server_batch_delay', type=int, default=0,
            help='amount of work server does for each batch of packets (in us)')
    parser.add_argument('--count_queue', type=int, default=1,
            help='number of queues available to the app')
    parser.add_argument('--queue_size', type=int, default=64,
            help='size of each queue')
    parser.add_argument('--slow-down', type=int, default=0,
            help='idle cycles to spend on the slow receiver server')
    parser.add_argument('--output', default=None, required=False,
            help="results will be writen in the given file")
    parser.add_argument('--vswitch_path', default=None, required=False,
            help="Should we override the vswitch path or not? Determine the path")
    parser.add_argument('--pci', type=str, help="PCI address of the Mellanox NICs")
    parser.add_argument('--time', type=int, help="Time of the experimnet", default=30)
    parser.add_argument('--tx_size', type=int, help="Server tx packet size", default=64)
    parser.add_argument('--drop', type=float, help="probability of drop in BESS", default=0.0)
    parser.add_argument('--delay', type=int,
            help="number of nanoseconds a client should wait before sending the packets", default=2000)

    args = parser.parse_args()

    # source_ip = args.source_ip
    count_core = args.count_core
    count_queue = args.count_queue
    queue_size = args.queue_size
    server_batch_delay = args.server_batch_delay
    override_vswitch_path=args.vswitch_path
    pci = args.pci
    vhost_port_count = args.count_vhost_port
    slow_down = args.slow_down
    time = args.time
    tx_size = args.tx_size
    mode = args.mode.strip()
    drop_probability = args.drop
    delay = args.delay
    main()

