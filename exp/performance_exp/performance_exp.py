#! /usr/bin/python3
import os
import sys
import time
import yaml
import subprocess

sys.path.insert(0, "../")
from bkdrft_common import (bessctl_do, get_docker_container_logs,
        docker_container_is_running)
from tas_containers import spin_up_memcached, run_udp_app


# Reserve some cores for Bess
used_cores = 6
max_cores = os.cpu_count()
def get_core(count):
    """
    This function assigns some cores for
    each program
    """
    global used_cores
    r = []
    for i in range(count):
        used_cores += 2
        r.append(str(used_cores % max_cores))
        if used_cores > max_cores:
            print('Warning cores are shared between programs')
    return ','.join(r)


def remote_run(addr, prog_cmd):
    cmd = ['ssh', addr, prog_cmd]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return proc


def stop_everything(containers):
    print('stop any thing running from previous tries')
    for c in containers:
        name = c['name']
        image = c.get('image')
        raddr = c.get('remote_addr')
        if raddr:
            continue
        if not image  or image == 'udp_app':
            subprocess.run('sudo pkill udp_app', shell=True)
            continue
        subprocess.run('sudo docker stop -t 0 {}'.format(name),
                shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        subprocess.run('sudo docker rm {}'.format(name),
                shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    bessctl_do('daemon stop', stderr=subprocess.PIPE)
    time.sleep(1)


def setup_pipeline():
    print('start bess daemon')
    bessctl_do('daemon start')
    print('=' * 40)
    print('  Performance Experiment')
    print('=' * 40)

    # pipeline_file = os.path.join('./pipelines', 'simple_pipeline.bess')
    # pipeline_file = os.path.join('./pipelines', 'bp_pipeline.bess')
    # pipeline_file = os.path.join('./pipelines', 'dpfq_pipeline.bess')
    pipeline_file = os.path.join('./pipelines', 'bp_dpfq_pipeline.bess')
    print('experiment pipeline:', pipeline_file)
    ret = bessctl_do('daemon start -- run file {}'.format(pipeline_file))
    if ret.returncode:
        print('Failed to start Bess pipeline')
        sys.exit(1)
    ret = bessctl_do('show pipeline')
    time.sleep(1)


def run_service(conf):
    raddr = conf.get('remote_addr')
    if not raddr:
        if conf['image'] == 'tas_memcached':
            p = spin_up_memcached(conf)
        elif conf['image'] == 'udp_app':
            p = run_udp_app(conf)
        else:
            print('Warning: Unknown image name')
            return
        conf['proc'] = p
    else:
        if conf['image'] == 'mutilate':
            start_mutilate = f'''cd $HOME/mutilate/; ./mutilate -s \
            {conf['dst_ip']} -t {conf['duration']} -T {conf['threads']} -c \
            {conf['connections']};'''
            print(start_mutilate)
            p = remote_run(raddr, start_mutilate)
            conf['proc'] = p


def start_servers(containers):
    for c in containers:
        if c['type'] != 'server':
            continue
        run_service(c)


def start_clients(containers):
    for c in containers:
        if c['type'] != 'client':
            continue
        run_service(c)


def log_service_output(services):
    print('Program outputs:\n')
    for c in services:
        name = c['name']
        raddr = c.get('remote_addr')
        if raddr:
            print(f'++++ {name} ++++')
            p = c['proc']
            out = p.communicate()
            for o in out:
                print(o.decode())
        else:
            img = c.get('image')
            if not img or img == 'udp_app':
                txt = c['proc'].communicate()[0].decode()
            else:
                txt = get_docker_container_logs(name)
            print('++++ {} ++++'.format(name))
            print(txt)
    print('\n\n')


def log_vswitch():
    count_progs = 2
    ports = ['ex_vhost{}'.format(i) for i in range(count_progs)]
    ports.append('pmd_port0')
    for p in ports:
        cmd = 'show port {0}'.format(p)
        p = bessctl_do(cmd, stdout=subprocess.PIPE)
        txt = p.stdout.decode()
        print(txt)



def wait_for_interrupt():
    print('After the experiment hit Ctrl-C')
    try:
        while True:
            time.sleep(60)
    except KeyboardInterrupt as e:
        pass


def main():
    with open('./exp.yaml', 'r') as f:
        services = yaml.safe_load(f)
    stop_everything(services)
    setup_pipeline()
    start_servers(services)
    start_clients(services)
    wait_for_interrupt()
    # time.sleep(services[2]['duration'] + 5)
    log_service_output(services)
    log_vswitch()
    print('--- Not gathering pause_calls ---')
    stop_everything(services)


if __name__ == '__main__':
    main()

    # cmd = '''for i in `seq 5`; do 
    # echo $i
    # done'''
    # p = remote_run('fshahi5', cmd)
    # c = p.communicate()
    # print(c[0].decode())
    # print(c[1])

