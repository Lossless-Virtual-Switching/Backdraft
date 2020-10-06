#!/usr/bin/python3 
from time import sleep

import os
import sys
import argparse
import subprocess
from exp_result import Result


cur_script_dir = os.path.dirname(os.path.abspath(__file__))
bessctl_dir = os.path.abspath(os.path.join(cur_script_dir, '../../code/bess-v1/bessctl'))
bessctl_bin = os.path.join(bessctl_dir, 'bessctl')

pipeline_config_temp = os.path.join(cur_script_dir, 'pipeline_config_template.txt')
pipeline_config_file = os.path.join(cur_script_dir, 'more_ports.bess')

shenango_netperf = os.path.abspath(os.path.join(cur_script_dir,
        '../../code/apps/dpdk_netperf/build/dpdk_netperf'))


def bessctl_do(command, stdout=None):
    """
    """
    cmd = '{} {}'.format(bessctl_bin, command)
    ret = subprocess.run(cmd, shell=True, stdout=subprocess.PIPE)
    return ret


def update_config(cnt_ports, cnt_queues, _type, agent):
    with open(pipeline_config_temp, 'r') as f:
        content = f.readlines()
    index = content.index('# == Insert here ==\n') + 1
    # content.insert(index, 'EXCESS = {}\n'.format(excess_ports))
    content.insert(index, 'CNT_P = {}\n'.format(cnt_ports))
    content.insert(index, 'CNT_Q = {}\n'.format(cnt_queues))
    content.insert(index, 'TYPE = {}\n'.format(_type))
    content.insert(index, 'AGENT = {}\n'.format(agent))
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


def run_netperf_server(cnt_queue):
    """
    Shenango dpdk_netperf server
    """
    # prefix = 'shenango_netperf_server'
    prefix = 'bessd-dpdk-prefix'
    cpu = 4
    vdev = 'virtio_user0,path=/tmp/ex_vhost0.sock,queues=8'
    ip = '192.168.1.2'
    args = {
            'bin': shenango_netperf,
            'cpu': cpu,
            'file-prefix': prefix,
            'vdev': vdev,
            'ip': ip,
            'cnt_q': cnt_queue,
            }
    # cmd = 'sudo {bin} -l{cpu} --file-prefix={file-prefix} --vdev="{vdev}" --socket-mem=128 -- UDP_SERVER {ip} {cnt_q}'.format(**args)
    cmd = ('sudo {bin} -l{cpu} --file-prefix={file-prefix} --proc-type=secondary'
            '--socket-mem=128 -- vport= UDP_SERVER {ip} '
            '{cnt_q}').format(**args)
    # Run in background
    p = subprocess.Popen(cmd, shell=True)
    return p


def run_netperf_client(bkdrft, cnt_queue):
    """
    Shenango dpdk_netperf client 
    """
    prefix = 'shenango_netperf_client'
    cpu = 5
    vdev = 'virtio_user1,path=/tmp/ex_vhost1.sock,queues=8'
    client_ip = '192.168.1.3'
    server_ip = '192.168.1.2'
    duration = 10
    payload_size = 8
    args = {
            'bin': shenango_netperf,
            'cpu': cpu,
            'file-prefix': prefix,
            'vdev': vdev,
            'client_ip': client_ip,
            'server_ip': server_ip,
            'duration': duration,
            'payload_size': payload_size,
            'bkdrft': bkdrft,
            'cnt_q': cnt_queue,
            }
    cmd = 'sudo {bin} -l{cpu} --file-prefix={file-prefix} --vdev="{vdev}" --socket-mem=128 -- UDP_CLIENT {client_ip} {server_ip} 50000 8001 {duration} {payload_size} {bkdrft} {cnt_q}'.format(**args)
    
    # Run in background
    p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
    return p


def parse_client_stdout(txt):
    """
    Parse the netperf stdout and create
    a result object. This object is 
    used in analysis and report generation
    process.
    """
    r = Result.from_netperf_stdout(txt)
    return r


def add_bess_results(res):
    # Get bess drops
    p = bessctl_do('show port ex_vhost0', subprocess.PIPE)
    txt = p.stdout.decode().split('\n')
    x = txt[5].split()[1]
    x = int(x.replace(',',''))
    res.bess_drops = x
    


def generate_report_file(results, output_file):
    with open(output_file, 'w') as f:
        for r in results:
            txt = r.generate_report()
            f.write(txt)
            f.write('==============\n')


def main():
    """
    About experiment
    This experiment investigates the overhead of adding
    more PMDPorts to bess software switch
    """

    # Run bess daemon
    print('start bess daemon')
    ret = bessctl_do('daemon start')
    if ret.returncode != 0:
        print('failed to start bess daemon')
        return 1

    #sleep(2)

    cnt_prt_q = [(2,2), (4,2), (8, 2), (2, 8), (4, 8), (8, 8), (16, 8)]
    cnt_prt_q = [(2,2),]
    # cnt_prt_q = [0]
    # Warning: SINGLE_PMD_MULTIPLE_Q is not supported any more. (it needs EXCESS variable to be defined)
    exp_types = ['MULTIPLE_PMD_MULTIPLE_Q',] # 'SINGLE_PMD_MULTIPLE_Q']
    agents = ['BKDRFT', 'BESS']
    for _type in exp_types:
        for agent in agents:
            results = []
            for cnt_ports, cnt_queues in cnt_prt_q:
                print('==============================')
                print('TYPE={}\tAGENT={}\tPORTS={}\tQ={}'.format(_type, agent, cnt_ports, cnt_queues))
                # Update configuration
                update_config(cnt_ports, cnt_queues, _type, agent)

                # Run a configuration (pipeline)
                remove_socks()
                file_path = pipeline_config_file
                ret = bessctl_do('daemon start -- run file {}'.format(file_path))

                # Run netperf server
                server_p = run_netperf_server(cnt_queues)

                # Run client
                client_p = run_netperf_client(agent.lower(), cnt_queues)

                # Wait
                client_p.wait()
                txt = str(client_p.stdout.read().decode())
                subprocess.run('sudo pkill dpdk_netperf', shell=True)  # Stop server
                res = parse_client_stdout(txt)
                res.set_excess_ports((cnt_queues * cnt_ports) - 2)
                add_bess_results(res)
                results.append(res)
                bessctl_do('daemon stop')
                generate_report_file(results, './results/{}_{}_results.txt'.format(_type, agent))


if __name__ == '__main__':
    main()

