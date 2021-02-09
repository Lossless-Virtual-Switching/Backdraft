import os
import subprocess


def _log_cmd(cmd):
    print('=' * 40)
    print(cmd)
    print('=' * 40)


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
    _log_cmd(cmd)
    return subprocess.check_call(cmd, shell=True, stdout=subprocess.PIPE)


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
    _log_cmd(cmd)
    return subprocess.check_call(cmd, shell=True, stdout=subprocess.PIPE)


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
    _log_cmd(cmd)
    return subprocess.check_call(cmd, shell=True, stdout=subprocess.PIPE)


def spin_up_memcached_shuffle(conf):
    here = os.path.dirname(__file__)
    tas_spinup_script = os.path.abspath(os.path.join(here,
        '../code/apps/tas_memcached_shuffle/spin_up_tas_container.sh'))
    # image_name = 'tas_memcached_shuffle'

    cmd = ('{tas_script} {name} {image} {cpu} {cpus:.2f} '
            '{socket} {ip} {tas_cores} {tas_queues} {prefix} {cdq} '
            '{type} ').format(tas_script=tas_spinup_script, **conf)
    if conf['type'] == 'client':
        cmd = ('{cmd} {dst_ip} {duration} {warmup_time} {wait_before_measure} '
               '{threads} {connections} {shuffle_size}').format(cmd=cmd, **conf)
    elif conf['type'] == 'server':
        cmd = '{cmd} {memory} {threads}'.format(cmd=cmd, **conf)
    else:
        raise Exception('Container miss configuration: '
                        'expecting type to be client or server')
    _log_cmd(cmd)
    return subprocess.check_call(cmd, shell=True, stdout=subprocess.PIPE)


def spin_up_shuffle(conf):
    """
    general:
      1. name
      2. image
      3. cpu
      4. cpus
      5. socket
      6. ip
      7. tas_cores
      8. tas_queues
      9. prefix
      10. cdq
      11. type
    client:
      1. server_req_unit
      2. count_flow
      3. dst_ip
      4. server_port
    server:
      1. port
    """
    here = os.path.dirname(__file__)
    tas_spinup_script = os.path.abspath(os.path.join(here,
        '../code/apps/tas_shuffle/spin_up_tas_container.sh'))
    # image_name = 'tas_shuffle'

    cmd = ('{tas_script} {name} {image} {cpu} {cpus:.2f} '
            '{socket} {ip} {tas_cores} {tas_queues} {prefix} {cdq} '
            '{type} ').format(tas_script=tas_spinup_script, **conf)
    if conf['type'] == 'client':
        cmd = ('{cmd} {server_req_unit} {count_flow} {dst_ip} {server_port} '
               ).format(cmd=cmd, **conf)
    elif conf['type'] == 'server':
        cmd = '{cmd} {port}'.format(cmd=cmd, **conf)
    else:
        raise Exception('Container miss configuration: '
                        'expecting type to be client or server')
    _log_cmd(cmd)
    return subprocess.check_call(cmd, shell=True, stdout=subprocess.PIPE)


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
    * bidi
    * --- server ---
    * delay
    * ---- client ---
    * ips
    * count_flow
    * duration
    * port
    * delay: processing time for each packet
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
      'bidi': 'false',
    }
    conf.update(config)

    mode = conf['type']
    # argv = ['sudo', udp_app, '--no-pci', '-l', conf['cpu'],
    #         '--file-prefix={}'.format(conf['prefix']),
    #         '--vdev={}'.format(conf['vdev']), '--socket-mem=128', '--']
    if mode  == 'server':
        cmd = ('sudo {bin} --no-pci --lcores="{cpu}" --file-prefix={prefix} '
                'bidi={bidi} --vdev="{vdev}" --socket-mem=128 -- '
                '{ip} {count_queue} {sysmod} {mode} {delay}'
              ).format(bin=udp_app, mode=mode, **conf)
        # params = [conf['ip'], conf['count_queue'], conf['sysmod'], conf['mode'],
        #         conf['delay']]
        # argv += params
    elif mode == 'client':
        ips = conf['ips']
        count_ips = len(ips)
        conf['cnt_ips'] = count_ips
        conf['ips'] = ' '.join(ips)
        cmd = ('sudo {bin} --no-pci --lcores="{cpu}" --file-prefix={prefix} '
                '--vdev="{vdev}" --socket-mem=128 -- '
                'bidi={bidi} {ip} {count_queue} {sysmod} {mode} {cnt_ips} {ips} '
                '{count_flow} {duration} {port} {delay}'
              ).format(bin=udp_app, mode=mode, **conf)
        if 'rate' in conf:
            cmd += ' {}'.format(conf['rate'])
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
    FNULL = subprocess.PIPE
    _log_cmd(cmd)
    proc = subprocess.Popen(cmd, shell=True, close_fds=True,
                            stdout=FNULL, stderr=subprocess.STDOUT)
    return proc


def run_dummy_app(config):
    """
    Run dummy application
    it does not use container for running
    config fields:
    * -- general --
    * cpu: on which core
    * prefix: dpdk file perfix
    * vdev: which port to connect
    * ---- app ----
    * count_queue
    * cdq: should use doorbell queue
    * process_cost: cycles per packet (target pkt)
    * ips: list of ips e.g. ['10.10.0.1',...] (target ips)
    """
    here = os.path.dirname(__file__)
    app = os.path.abspath(os.path.join(here,
            '../code/apps/dummy/build/dummy_app'))

    # default values
    conf = {
      'process_cost': 0,
    }
    conf.update(config)

    ips = conf['ips']
    count_ips = len(ips)
    conf['cnt_ips'] = count_ips
    conf['ips'] = ' '.join(ips)

    cmd = ('sudo {bin} --no-pci -l{cpu} --file-prefix={prefix} '
            '--vdev="{vdev}" --socket-mem=128 -- '
            '{count_queue} {cdq} {process_cost} {cnt_ips} {ips}'
          ).format(bin=app, **conf)

    FNULL = open(os.devnull, 'w')  # pipe output to null
    FNULL = None
    _log_cmd(cmd)
    proc = subprocess.Popen(cmd, shell=True, close_fds=True,
                            stdout=FNULL, stderr=subprocess.STDOUT)
    return proc

