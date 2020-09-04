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
        cmd = '{cmd} {server_ip} {threads} {connections} {message_size}'.format(cmd=cmd, **conf)
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

