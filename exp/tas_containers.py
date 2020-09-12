import os
import subprocess


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

    here = os.path.dirname(__file__)
    tas_spinup_script = os.path.abspath(os.path.join(here,
        '../code/apps/tas_container/spin_up_tas_container.sh'))
    # image_name = 'tas_container'

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
            '{message_per_sec} {message_size} {flow_num_msg} {count_threads}'
            ).format(
        tas_script=tas_spinup_script, count_ips=count_ips, _ips=_ips, **conf)
    # print(cmd)
    return subprocess.run(cmd, shell=True, stdout=subprocess.PIPE)


def spin_up_unidir(conf):
    here = os.path.dirname(__file__)
    tas_spinup_script = os.path.abspath(os.path.join(here,
        '../code/apps/tas_unidir/spin_up_tas_container.sh'))
    # image_name = 'tas_unidir'

    cmd = ('{tas_script} {name} {image} {cpu} {cpus:.2f} '
           '{socket} {ip} {tas_cores} {tas_queues} {prefix} {cdq} '
           '{type} ').format(tas_script=tas_spinup_script, **conf)
    if conf['type'] == 'client':
        cmd = '{cmd} {server_ip} {threads} {connections} {message_size} {server_delay_cycles}'.format(cmd=cmd, **conf)
    elif conf['type'] == 'server':
        cmd = '{cmd} {threads} {connections} {message_size} {server_delay_cycles}'.format(cmd=cmd, **conf)
    else:
        raise Exception('Container miss configuration: '
                        'expecting type to be client or server')
    # print(cmd)
    return subprocess.run(cmd, shell=True, stdout=subprocess.PIPE)


def spin_up_memcached(conf):
    here = os.path.dirname(__file__)
    tas_spinup_script = os.path.abspath(os.path.join(here,
        '../code/apps/tas_memcached/spin_up_tas_container.sh'))
    # image_name = 'tas_memcached'

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


def run_udp_app(config):
    """
    Run UDP application
    it does not use container for running
    config fields:
    * -- general --
    * type
    * cpu
    * prefix
    * vdev
    * ---- app ----
    * ip
    * count queue
    * system mode (bess, bkdrft)
    * --- server ---
    * delay
    * ---- client ---
    * ips
    * count_flow
    * duration
    * port
    """
    here = os.path.dirname(__file__)
    udp_app = os.path.abspath(os.path.join(here,
            '../code/apps/udp_client_server/build/udp_app'))

    # default values
    conf = {
      'dely': 0,
      'count_flow': 1,
      'duration': 40,
      'port': 5000,
    }
    conf.update(config)

    ips = conf['ips']
    count_ips = len(ips)
    conf['cnt_ips'] = count_ips
    conf['ips'] = ' '.join(ips)

    mode = conf['type']
    # argv = ['sudo', udp_app, '--no-pci', '-l', conf['cpu'],
    #         '--file-prefix={}'.format(conf['prefix']),
    #         '--vdev={}'.format(conf['vdev']), '--socket-mem=128', '--']
    if mode  == 'server':
        cmd = ('sudo {bin} --no-pci -l{cpu} --file-prefix={prefix} '
                '--vdev="{vdev}" --socket-mem=128 -- '
                '{ip} {count_queue} {sysmod} {mode} {delay}'
              ).format(bin=udp_app, mode=mode, **conf)
        # params = [conf['ip'], conf['count_queue'], conf['sysmod'], conf['mode'],
        #         conf['delay']]
        # argv += params
    elif mode == 'client':
        cmd = ('sudo {bin} --no-pci -l{cpu} --file-prefix={prefix} '
                '--vdev="{vdev}" --socket-mem=128 -- '
                '{ip} {count_queue} {sysmod} {mode} {cnt_ips} {ips} '
                '{count_flow} {duration} {port}'
              ).format(bin=udp_app, mode=mode, **conf)
        # params = [conf['ip'], conf['count_queue'], conf['sysmod'], mode,
        #           count_ips, conf['ips'], conf['count_flow'], conf['duration'],
        #           conf['port']]
        # argv += params
    else:
        raise ValueError('type value should be either server or client')

    # pid = os.fork()
    # if pid == 0:
    #     os.execvp('sudo', argv)
    # print(cmd)
    FNULL = open(os.devnull, 'w') # pipe output to null
    FNULL = None
    # print(cmd)
    proc = subprocess.Popen(cmd, shell=True, close_fds=True,
                            stdout=FNULL, stderr=subprocess.STDOUT)
    return proc

