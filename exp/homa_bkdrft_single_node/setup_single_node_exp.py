#!/usr/bin/python3
"""
Backdraft Homa Evaluation Experiment
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
    'single_node_pipeline.bess')

homa_base = os.path.join(cur_script_dir, '../../code/Homa')
homa_app_bin = os.path.join(homa_base, 'build/test/dpdk_test') # udp_app


def _stop_everything():
    try:
      bessctl_do('daemon stop') # this may show an error it is nothing
    except:
      # bess was not running pass
      pass

    subprocess.run('sudo pkill dpdk_test', shell=True)
    subprocess.run('sudo pkill dpdk_client_sys', shell=True)
    subprocess.run('sudo pkill dpdk_server_sys --signal 2', shell=True)


def run_client(conf):
    cmd = "sudo ./build/test/dpdk_test --vhost-port \
    --iface='--vdev=virtio_user0,path=/tmp/vhost_0.sock,queues=1'  192.168.1.9 \
    --dpdk-extra=--no-pci --dpdk-extra='-l 7' --slow-down={} --tx-pkt-length={}".format(conf["slow_down"], conf["tx_pkt_length"])

    cmd = "sudo ./build/test/dpdk_test --vhost-port \
    --iface='--vdev=virtio_user0,path={},queues=1' 192.168.1.9 \
    --dpdk-extra=--no-pci --dpdk-extra='-l {}' --dpdk-extra='--file-prefix=mp_{}' --vhost-port-ip={} --vhost-port-mac={} --slow-down={} --tx-pkt-length={}".format(conf['path'],
            conf['cpuset'],
            conf['file_prefix'],
            conf['ip'],
            conf['mac'],
            conf["slow_down"],
            conf["tx_pkt_length"])
    return subprocess.Popen(cmd, shell=True, cwd=homa_base)

def run_server(conf):
    # cmd = ('sudo ./udp_app -l {cpuset} --no-pci --file-prefix={prefix} --vdev="{vdev}" -- '
    #         '{source_ip} {count_queue} {type} server {delay}').format(**conf)
    cmd = "sudo ./build/test/dpdk_test --vhost-port \
    --iface='--vdev=virtio_user0,path=/tmp/vhost_0.sock,queues=1'  --server \
    --dpdk-extra=--no-pci --dpdk-extra='-l 6' --slow-down={} --tx-pkt-length={} --vhost-port-ip={} --vhost-port-mac={}".format(conf["slow_down"], conf["tx_pkt_length"],
            conf["ip"], conf["mac"])
    return subprocess.Popen(cmd, shell=True, cwd=homa_base)

def run_system_perf_client(conf):
    # cmd = "sudo ./build/test/dpdk_test --vhost-port \
    # --iface='--vdev=virtio_user0,path=/tmp/vhost_0.sock,queues=1'  --server \
    # --dpdk-extra=--no-pci --dpdk-extra='-l 6' --slow-down={} --tx-pkt-length={} --vhost-port-ip={} --vhost-port-mac={}".format(conf["slow_down"], conf["tx_pkt_length"],
    #         conf["ip"], conf["mac"])

    cmd = "sudo ./build/test/dpdk_client_system_test 100000 -v --vhost-port \
    --iface='--vdev=virtio_user0,path={}' --dpdk-extra=--no-pci \
    --size=10000 --dpdk-extra='--file-prefix=mg-{} '--dpdk-extra='-l {}' --vhost-port-ip={} --vhost-port-mac={}".format(conf["path"], conf["ip"], conf["cpuset"], conf["ip"], conf["mac"])

    print("client {}".format(cmd))

    return subprocess.Popen(cmd, shell=True, cwd=homa_base,
             stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding='utf8')

def run_system_perf_server(conf):
    # cmd = "sudo ./build/test/dpdk_test --vhost-port \
    # --iface='--vdev=virtio_user0,path=/tmp/vhost_0.sock,queues=1'  --server \
    # --dpdk-extra=--no-pci --dpdk-extra='-l 6' --slow-down={} --tx-pkt-length={} --vhost-port-ip={} --vhost-port-mac={}".format(conf["slow_down"], conf["tx_pkt_length"],
    #         conf["ip"], conf["mac"])

    cmd = "sudo ./build/test/dpdk_server_system_test 100 --server=1 -v \
    --vhost-port --iface='--vdev=virtio_user0,path={}' --dpdk-extra=--no-pci \
    --dpdk-extra='-l {}' --dpdk-extra='--file-prefix=mg-{}' --vhost-port-ip={} \
    --vhost-port-mac={}".format(conf['path'], conf['cpuset'], conf['cpuset'],
            conf["ip"], conf["mac"])

    if conf["slow_down"]:
        cmd = "sudo cpulimit -l {} -- ./build/test/dpdk_server_system_test 100 \
        --server=1 -v --vhost-port --iface='--vdev=virtio_user0,path={}' \
        --dpdk-extra=--no-pci --dpdk-extra='-l {}' \
        --dpdk-extra='--file-prefix=mg-{}' --vhost-port-ip={} \
        --vhost-port-mac={}".format(conf["slow_down"], conf['path'],
                conf['cpuset'], conf['cpuset'], conf["ip"], conf["mac"])

    print("server {}".format(cmd))

    return subprocess.Popen(cmd, shell=True, cwd=homa_base,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding='utf8')

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
    _stop_everything()
    # Write pipeline config file
    config_path = '/tmp/.pipeline_config.json'
    if os.path.exists(config_path):
        try:
            pipeline_conf = json.load(open(config_path))
        except:
            pipeline_conf = {}
    else:
        pipeline_conf = {}

    with open(config_path, 'w') as f:
      # pipeline_conf['count_core'] = count_core
      pipeline_conf['count_queue'] = count_queue
      pipeline_conf['queue_size'] = queue_size
      # pipeline_conf['pci_address'] = pci
      pipeline_conf['client_count'] = args.client_count
      pipeline_conf['server_count'] = args.server_count
      txt = json.dumps(pipeline_conf)
      f.write(txt)

    if override_vswitch_path:
        override_bess_path(override_vswitch_path)

    # Setup BESS config
    file_path = pipeline_config_file
    ret = bessctl_do('daemon start -- run file {}'.format(file_path))
    if ret.returncode != 0:
      print("bess has issues")
      return 1


    server_process = []
    client_process = []

    for i in range(args.server_count):
        name = 'server_port_{}'.format(i)
        server_conf = {
                'cpuset': i + 32,
                'path': '/tmp/vhost_{}.sock,queues={}'.format(name, count_queue),
                'mac': '1c:34:da:41:c6:fc',
                'ip': '192.168.1.1',
                'slow_down': slow_down,
                'tx_pkt_length': tx_size
        }
        sp = run_system_perf_server(server_conf)
        server_process.append(sp)

    for i in range(args.client_count):
        # print('ip ' + "192.168.1.{}".format(i + 1))
        name = 'client_port_{}'.format(i)
        client_conf = {
          'cpuset': i+7, # it is just random
          'path': '/tmp/vhost_{}.sock,queues={}'.format(name, count_queue),
          'slow_down': slow_down,
          'tx_pkt_length': tx_size,
          'file_prefix': i,
          'ip': "192.168.1.{}".format(i + 2),
          'mac': "1c:34:da:41:d0:0c"
        }

        # run_client(client_conf)
        cp = run_system_perf_client(client_conf)
        client_process.append(cp)


    # Wait until all clients are finished
    try:
        for p in client_process:
            p.wait()
    except KeyboardInterrupt:
        for p in client_process:
            if p.poll() is None:
                p.send_signal(subprocess.signal.SIGINT)

    # Stop any remaining server
    for p in server_process:
        if p.poll() is None:
            p.send_signal(subprocess.signal.SIGINT)

    # ------------------------------------------------
    # pfc_stats_before = get_pfc_results()

    sum_pkts = 0
    sum_bytes = 0

    # Measuring the throughput
    ret = bessctl_do('command module measure_pps0 get_summary EmptyArg {}', subprocess.PIPE)
    if ret.returncode != 0:
        print("command measure pps failed")
    else:
        stdout = ret.stdout.decode()
        print('STDOUT:{}'.format(stdout))

    # ret = bessctl_do('show port', subprocess.PIPE)
    # log = ret.stdout.decode()
    # log = log.strip()
    # print(log)
    # ret = bessctl_do('command module measure_pps0 get_summary EmptyArg', subprocess.PIPE)
    # if ret.returncode != 0:
    #     print("command measure pps failed")
    # else:
    #     stdout = ret.stdout.decode()
    #     print('STDOUT:{}'.format(stdout))

    for i in range(args.client_count):
        name = 'client_port_{}'.format(i)
        ret = bessctl_do('show port {}'.format(name), subprocess.PIPE)
        # if(ret != 0):
        #   print("failed to run: " +'show port port_{}'.format(i))
        #   continue
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

        line = lines[4].strip()
        raw = line.split()
        pkts = float(raw[2].replace(',', ''))
        byte = float(raw[4].replace(',', ''))
        sum_pkts += pkts
        sum_bytes += byte

        # pause frame
        name = 'bpq_inc{}'.format(i)
        ret = bessctl_do('command module {} get_summary EmptyArg {{}}'.format(name), subprocess.PIPE)
        log = ret.stdout.decode()
        print(log)

        name = 'bpq_out{}'.format(i)
        ret = bessctl_do('command module {} get_summary EmptyArg {{}}'.format(name), subprocess.PIPE)
        log = ret.stdout.decode()
        print(log)

    # I don't have the time of experiment.
    print ('throughput: {:2f} (Mpps) {:2f} (Gbps)'.format(sum_pkts / 40 / 1e6, sum_bytes * 8 / 40 / 1e9))

    for i in range(args.server_count):
        name = 'server_port_{}'.format(i)
        ret = bessctl_do('show port {}'.format(name), subprocess.PIPE)
        # if(ret != 0):
        #   print("failed to run: " +'show port port_{}'.format(i))
        #   continue
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

        line = lines[4].strip()
        raw = line.split()
        pkts = float(raw[2].replace(',', ''))
        byte = float(raw[4].replace(',', ''))
        sum_pkts += pkts
        sum_bytes += byte

        # pause frame
        name = 'bpq_inc{}'.format(i)
        ret = bessctl_do('command module {} get_summary EmptyArg {{}}'.format(name), subprocess.PIPE)
        log = ret.stdout.decode()
        print(log)

        name = 'bpq_out{}'.format(i)
        ret = bessctl_do('command module {} get_summary EmptyArg {{}}'.format(name), subprocess.PIPE)
        log = ret.stdout.decode()
        print(log)




    # I don't have the time of experiment.
    print ('throughput: {:2f} (Mpps) {:2f} (Gbps)'.format(sum_pkts / 40 / 1e6, sum_bytes * 8 / 40 / 1e9))

    print("\n\n")
    print("CLIENTS:\n")
    for proc in client_process:
        stdout, stderr = proc.communicate()
        print('STDOUT:{}'.format(stdout))
        print('STDERR:{}'.format(stderr))

    _stop_everything()

    # print(server_process)
    print("\n\n")
    print("SERVERS:\n")
    for proc in server_process:
        stdout, stderr = proc.communicate()
        print('STDOUT:{}'.format(stdout))
        print('STDERR:{}'.format(stderr))

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

    # parser.add_argument('count_core', type=int)
    parser.add_argument('client_count', type=int, help="Number of client ports")
    parser.add_argument('server_count', type=int, help="Number of server ports")
    parser.add_argument('--count_queue', type=int, default=1,
            help='number of queues available to the app')
    parser.add_argument('--queue_size', type=int, default=64,
            help='size of each queue')
    parser.add_argument('--slow-down', type=int, default=0,
            help='idle cycles to spend on the slow receiver server')
    # parser.add_argument('--output', default=None, required=False,
    #         help="results will be writen in the given file")
    parser.add_argument('--vswitch_path', default=None, required=False,
            help="Should we override the vswitch path or not? Determine the path")
    # parser.add_argument('--pci', type=str, help="PCI address of the Mellanox NICs")
    # parser.add_argument('--time', type=int, help="Time of the experimnet", default=30)
    parser.add_argument('--tx_size', type=int, help="Server tx packet size", default=64)
    # parser.add_argument('--drop', type=float, help="probability of drop in BESS", default=0.0)

    args = parser.parse_args()

    # source_ip = args.source_ip
    # count_core = args.count_core
    count_queue = args.count_queue
    queue_size = args.queue_size
    # server_batch_delay = args.server_batch_delay
    override_vswitch_path=args.vswitch_path
    # pci = args.pci
    # vhost_port_count = args.count_vhost_port
    slow_down = args.slow_down
    # time = args.time
    tx_size = args.tx_size
    # mode = args.mode.strip()
    # drop_probability = args.drop
    main()

