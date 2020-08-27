#!/usr/bin/python3
import sys
from time import sleep
import os
import argparse
import subprocess
# from exp_result import Result


cur_script_dir = os.path.dirname(os.path.abspath(__file__))
bessctl_dir = os.path.abspath(os.path.join(cur_script_dir, '../../code/bess-v1/bessctl'))
bessctl_bin = os.path.join(bessctl_dir, 'bessctl')

pipeline_config_temp = os.path.join(cur_script_dir, 'pipeline_config_template.txt')
pipeline_config_file = os.path.join(cur_script_dir, 'slow_receiver.bess')

slow_receiver_exp = os.path.abspath(os.path.join(cur_script_dir,
        './client_server_src/build/slow_receiver_exp'))


def bessctl_do(command, stdout=None):
    """
    """
    cmd = '{} {}'.format(bessctl_bin, command)
    ret = subprocess.run(cmd, shell=True, stdout=subprocess.PIPE)
    return ret


def update_config():
    with open(pipeline_config_temp, 'r') as f:
        content = f.readlines()
    index = content.index('# == Insert here ==\n') + 1
    # content.insert(index, 'EXCESS = {}\n'.format(excess_ports))
    # content.insert(index, 'CNT_P = {}\n'.format(cnt_ports))
    # content.insert(index, 'CNT_Q = {}\n'.format(cnt_queues))
    # content.insert(index, 'TYPE = {}\n'.format(_type))

    if sysmod == 'bess':
        agent = 'BESS'
    elif sysmod == 'bkdrft':
        agent = 'BKDRFT'
    else:
        agent = 'BESS_BP'

    content.insert(index, 'AGENT = {}\n'.format(agent))
    content.insert(index, 'BP = {}\n'.format(str(bp)))
    content.insert(index, 'COMMAND_Q = {}\n'.format(str(cdq)))
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


def run_server(instance):
    """
        Start a server process
    """
    prefix = 'slow_receiver_server_{}'.format(instance)
    cpu = [6, 10][instance]  # on which cpu
    vdev = ['virtio_user0,path=/tmp/ex_vhost0.sock,queues=3',
            'virtio_user2,path=/tmp/ex_vhost2.sock,queues=3'][instance]
    server_delay = [0, slow][instance]
    # ip = '192.168.1.2'
    args = {
            'bin': slow_receiver_exp,
            'cpu': cpu,
            'file-prefix': prefix,
            'vdev': vdev,
	    'sysmod': 'bess' if sysmod == 'bess-bp' else sysmod,
            'mode': 'server',
            'inst': instance,
            'delay': server_delay,
            }
    cmd = 'sudo {bin} --no-pci -l{cpu} --file-prefix={file-prefix} --vdev="{vdev}" --socket-mem=128 -- {sysmod} {mode} {delay}'.format(**args)

    print("===============")
    print("     server    ")
    print(cmd)
    print("===============")
    # Run in background
    p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
    return p


def run_client(ips):
    """
        Start a client process
    """
    prefix = 'slow_receiver_exp_client'
    cpu = 14
    vdev = 'virtio_user1,path=/tmp/ex_vhost1.sock,queues=3'
    # client_ip = '192.168.1.3'
    # server_ip = '192.168.1.2'
    # duration = 10
    # payload_size = 8
    _ips = ' '.join(ips)
    args = {
            'bin': slow_receiver_exp,
            'cpu': cpu,
            'file-prefix': prefix,
            'vdev': vdev,
	    'sysmod': 'bess' if sysmod == 'bess-bp' else sysmod,
            'mode': 'client',
            'cnt_ips': len(ips),
            'ips':  _ips,
            # 'client_ip': client_ip,
            # 'server_ip': server_ip,
            # 'duration': duration,
            # 'payload_size': payload_size,
            # 'bkdrft': bkdrft,
            # 'cnt_q': cnt_queue,
            }
    cmd = 'sudo {bin} --no-pci -l{cpu} --file-prefix={file-prefix} --vdev="{vdev}" --socket-mem=128 -- {sysmod} {mode} {cnt_ips} {ips}'.format(**args)

    print("===============")
    print("     client")
    print(cmd)
    print("===============")

    # Run in background
    p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
    return p


def print_pps_from_info_log():
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
    print("=== pause call per sec ===")
    for name in book:
        print(name)
        for i, value in enumerate(book[name]):
            print(i, value)
    print("=== pause call per sec ===")


def main():
    """
    About experiment:
        BKDRFT Slow Receiver Exp.
    """

    # Kill anything running
    subprocess.run('sudo pkill slow_receiver_e', shell=True)  # Stop server
    bessctl_do('daemon stop') # this may show an error it is nothing

    # Run bess daemon
    print('start bess daemon')
    sleep(1)
    ret = bessctl_do('daemon start')
    if ret.returncode != 0:
        print('failed to start bess daemon')
        return 1

    #sleep(2)

    print('==============================')
    print('     Slow Receiver Exp.')
    print('==============================')

    # Update configuration
    update_config()

    # Run a configuration (pipeline)
    remove_socks()
    file_path = pipeline_config_file
    ret = bessctl_do('daemon start -- run file {}'.format(file_path))

    if bess_only:
        # Only run bess config
        return 0

    # Run server
    server_p1 = run_server(0)
    sleep(1)
    server_p2 = run_server(1)

    # Run client
    client_p = run_client(['192.168.2.10', '10.0.2.11'])

    # Wait
    client_p.wait()
    server_p1.wait()
    server_p2.wait()


    # Get output of processes
    txt0= str(client_p.stdout.read().decode())
    txt1 = str(server_p1.stdout.read().decode())
    txt2 = str(server_p2.stdout.read().decode())
    print('======')
    print(txt0)
    print('======')
    print(txt1)
    print('======')
    print(txt2)
    print('======')

    p = bessctl_do('show port ex_vhost0', subprocess.PIPE)
    txt = p.stdout.decode()
    print('server1\n', txt)

    p = bessctl_do('show port ex_vhost2', subprocess.PIPE)
    txt = p.stdout.decode()
    print('server2\n', txt)

    p = bessctl_do('show port ex_vhost1', subprocess.PIPE)
    txt = p.stdout.decode()
    print('client\n', txt)

    # res = parse_client_stdout(txt)
    # res.set_excess_ports((cnt_queues * cnt_ports) - 2)
    # add_bess_results(res)
    # results.append(res)
    bessctl_do('daemon stop')

    print_pps_from_info_log()
    # generate_report_file(results, './results/{}_{}_results.txt'.format(_type, agent))


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("usage: ./run_slow_receiver_exp.py [bess|bkdrft] [time] [bp] [bessonly]")
        sys.exit(1)
    sysmod = sys.argv[1]
    slow = int(sys.argv[2])
    bess_only = False
    bp = False
    cdq = False
    if ('bessonly' in sys.argv):
        bess_only = True
    if 'bp' in sys.argv:
        bp = True
    main()

