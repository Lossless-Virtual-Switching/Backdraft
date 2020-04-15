#!/usr/bin/python3 


import os
import sys
import argparse
import subprocess
from exp_result import Result


cur_script_dir = os.path.dirname(os.path.abspath(__file__))
bessctl_dir = os.path.abspath(os.path.join(cur_script_dir, '../../code/bess/bessctl'))
bessctl_bin = os.path.join(bessctl_dir, 'bessctl')

pipeline_config_temp = os.path.join(cur_script_dir, 'pipeline_config_template.txt')
pipeline_config_file = os.path.join(cur_script_dir, 'more_ports.bess')

shenango_netperf = os.path.abspath(os.path.join(cur_script_dir,
        '../../../shenango/apps/dpdk_netperf/build/dpdk_netperf'))


def bessctl_do(command, stdout=None):
    """
    """
    cmd = '{} {}'.format(bessctl_bin, command)
    ret = subprocess.run(cmd, shell=True, stdout=stdout)
    return ret


def update_config(excess_ports):
    assert isinstance(excess_ports, int)
    with open(pipeline_config_temp, 'r') as f:
        content = f.readlines()
    content.insert(0, 'EXCESS = {}\n'.format(excess_ports))
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


def run_netperf_server():
    """
    Shenango dpdk_netperf server
    """
    prefix = 'shenango_netperf_server'
    cpu = 4
    vdev = 'virtio_user0,path=/tmp/my_vhost1.sock,queues=1'
    ip = '192.168.1.2'
    args = {
            'bin': shenango_netperf,
            'cpu': cpu,
            'file-prefix': prefix,
            'vdev': vdev,
            'ip': ip,
            }
    cmd = 'sudo {bin} -l{cpu} --file-prefix={file-prefix} --vdev="{vdev}" --socket-mem=128 -- UDP_SERVER {ip}'.format(**args)
    # Run in background
    p = subprocess.Popen(cmd, shell=True)
    return p


def run_netperf_client():
    """
    Shenango dpdk_netperf client 
    """
    prefix = 'shenango_netperf_client'
    cpu = 5
    vdev = 'virtio_user1,path=/tmp/my_vhost2.sock,queues=1'
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
            }
    cmd = 'sudo {bin} -l{cpu} --file-prefix={file-prefix} --vdev="{vdev}" --socket-mem=128 -- UDP_CLIENT {client_ip} {server_ip} 50000 8001 {duration} {payload_size}'.format(**args)
    
    # Run in background
    p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
    return p


def parse_client_stdout(txt, cnt_excess_prt):
    """
    Parse the netperf stdout and create
    a result object. This object is 
    used in analysis and report generation
    process.
    """
    r = Result.from_netperf_stdout(txt)
    r.set_excess_ports(cnt_excess_prt)
    return r


def add_bess_results(res):
    # Get bess drops
    p = bessctl_do('show port my_vhost1', subprocess.PIPE)
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

    exp_count_excess_ports = [0] + [2 ** i for i in range(5)] + [30]
    # exp_count_excess_ports = [0]
    results = []
    for cnt_excess_prt in exp_count_excess_ports:
        # Update configuration
        update_config(cnt_excess_prt)

        # Run a configuration (pipeline)
        remove_socks()
        file_path = pipeline_config_file 
        ret = bessctl_do('daemon reset -- run file {}'.format(file_path))

        # Run netperf server
        server_p = run_netperf_server()

        # Run client
        client_p = run_netperf_client()

        # Wait
        client_p.wait()
        txt = str(client_p.stdout.read().decode())
        subprocess.run('sudo pkill dpdk_netperf', shell=True)  # Stop server
        res = parse_client_stdout(txt, cnt_excess_prt)
        add_bess_results(res)
        results.append(res)
    bessctl_do('daemon stop')
    generate_report_file(results, 'results.txt')


if __name__ == '__main__':
    main()
    
