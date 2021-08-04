# Backdraft common functionalities used in experiments

import os
import sys
import subprocess


# some paths
__cur_script_dir = os.path.dirname(os.path.abspath(__file__))
__bess_dir = os.path.abspath(os.path.join(__cur_script_dir,
    '../code/bess-v1'))
__bessctl_dir = os.path.join(__bess_dir, 'bessctl')
__bessctl_bin = os.path.join(__bessctl_dir, 'bessctl')
__bess_kmod_dir = os.path.join(__bess_dir, 'core/kmod')


# ----------- BESS --------------
def bessctl_do(command, stdout=None, stderr=None, cpu_list=None, env=None):
    """
    Run bessctl command
    """
    cmd = '{} {}'.format(__bessctl_bin, command)
    if cpu_list is not None:
        assert isinstance(cpu_list, str)
        cmd  = 'taskset -c {} {}'.format(cpu_list, cmd)
    ret = subprocess.run(cmd, shell=True, stdout=stdout, stderr=stderr, env=env)
    return ret


def setup_bess_pipeline(pipeline_config_path, env=None):
    """
    Returns boolean
    """
    # Make sure bessctl daemon is down
    ret = bessctl_do('daemon stop', stderr=subprocess.PIPE)
    # Might not be running at all
    # if ret.stderr is not None:
    #     return False

    # Run BESS config
    # ret = bessctl_do('daemon start')
    # if ret.returncode != 0:
    #     print('failed to start bess daemon', file=sys.stderr)
    #     return -1
    # Run a configuration (pipeline)
    cmd = 'daemon start -- run file {}'.format(pipeline_config_path)
    ret = bessctl_do(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    print(ret.stdout.decode())
    print(ret.stderr.decode())
    return not ret.stderr


def load_bess_kmod():
  cmd = './install'
  return subprocess.check_call(cmd, shell=True, cwd=__bess_kmod_dir) 


def setup_tas_engine(cfg, cpuset=None):
    """
    brnig tas engine up
    * cfg: dictionary
    {
        dpdk-w :
        dpdk-vdev:
        fp-no-xsumoffload:
        fp-no-autoscale:
        ip-addr:
        fp-cores-max:
        ...
    }
    """
    tas_dir = os.path.abspath(os.path.join(__cur_script_dir, '../code/tas'))
    tas_binary = os.path.join(tas_dir, 'tas/tas')
    print('* bin: ', tas_binary)
    dpdk_extra = ['--dpdk-extra=-w', '--dpdk-extra="41:00.0"']
    args = []
    for key, value in cfg.items():
        if  key.startswith('dpdk-'):
            a1 = key[5:]
            if not a1:
                print('warning unknown config:', key)
                continue
            args.append('--dpdk-extra=-{}'.format(a1))
            if not isinstance(value, bool):
                args.append('--dpdk-extra={}'.format(value))
            continue
        if isinstance(value, bool):
            args.append('--{}'.format(key))
        else:
            args.append('--{}={}'.format(key, value))
    cmd = [tas_binary, ] + args
    if cpuset is not None:
        cmd = ['taskset', '-c', cpuset] + cmd
    print('TAS engine:', cmd)
    proc = subprocess.Popen(cmd)
    return proc


# def set_cpu_affinity(pid, cpulist):
#     """
#     Using taskset for setting cpu affinit
#     pid: pid
#     cpulist: string, 1,2,3
#     """
#     cmd = 'taskset -a -p {}'.format(cpulist)
#     subprocess.check_call(cmd, shell=True)


def get_port_packets(port_name):
    p = bessctl_do('show port {}'.format(port_name), subprocess.PIPE)
    txt = p.stdout.decode()
    txt = txt.strip()
    lines = txt.split('\n')
    count_line = len(lines)
    res = { 'rx': { 'packet': -1, 'byte': -1, 'drop': -1},
            'tx': {'packet': -1, 'byte': -1, 'drop': -1}}
    if count_line < 6:
        return res

    raw = lines[2].split()
    res['rx']['packet'] = int(raw[2].replace(',',''))
    res['rx']['byte'] = int(raw[4].replace(',',''))
    raw = lines[3].split() 
    res['rx']['drop'] = int(raw[1].replace(',',''))


    raw = lines[4].split()
    res['tx']['packet'] = int(raw[2].replace(',',''))
    res['tx']['byte'] = int(raw[4].replace(',',''))
    raw = lines[5].split() 
    res['tx']['drop'] = int(raw[1].replace(',',''))
    return res


def get_pfc_results(interface):
    cmd = 'ethtool -S {} | grep prio3_pause'.format(interface)
    log = subprocess.check_output(cmd, shell=True)
    log = log.decode()
    log = log.strip()
    lines = log.split('\n')
    res = {}
    for line in lines:
        line = line.strip()
        key, value = line.split(':')
        res[key] = int(value)
    return res


def delta_dict(before, after):
    res = {}
    for key, value in after.items():
        if isinstance(value, dict):
            res[key] = delta_dict(before[key], value)
        elif isinstance(value, (int, float)):
            res[key] = value - before[key]
    return res


def get_pps_from_info_log():
    """
    Go through /tmp/bessd.INFO file and find packet per second reports.
    Then organize and print them in stdout.
    """
    book = dict()
    with open('/tmp/bessd.INFO') as log_file:
        for line in log_file:
            if 'pcps' in line:
                raw = line.split()
                value = raw[5]
                name = raw[7]
                if name not in book:
                    book[name] = list()
                book[name].append(float(value))
    return book


def str_format_pps(book):
    res = []
    res.append("=== pause call per sec ===")
    for name in book:
        res.append(name)
        for i, value in enumerate(book[name]):
            res.append('{} {}'.format(i, value))
    res.append("=== pause call per sec ===")
    txt = '\n'.join(res)
    return txt


# ----------- Container --------------
def get_container_pid(container_name):
    cmd = "sudo docker inspect --format '{{.State.Pid}}' " + container_name 
    pid = int(subprocess.check_output(cmd, shell=True))
    return pid


def docker_container_is_running(container_name):
    cmd = 'sudo docker ps | grep {}'.format(container_name) # not reliable
    try: 
        result = subprocess.check_output(cmd, shell=True)
        result = result.strip()
        # print(result)
        return result != ''
    except subprocess.CalledProcessError as error:
        if error.returncode == 1:
            return False  # no line was selected
        raise error


def get_docker_container_logs(container_name):
    cmd = 'sudo docker logs {name}'
    cmd = cmd.format(name=container_name)
    result = subprocess.check_output(cmd, shell=True)
    result = result.decode()
    return result


# ----------- Logger --------------
class Logger:
    def __init__(self, output=None):
        self._output_file = output

    def log(self, *args, end='\n'):
        for arg in args:
            self._output_file.write(arg)
            if end:
                self._output_file.write(end)

