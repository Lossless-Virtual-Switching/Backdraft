# Backdraft common functionalities used in experiments

import os
import subprocess


# some paths
__cur_script_dir = os.path.dirname(os.path.abspath(__file__))
__bess_dir = os.path.abspath(os.path.join(__cur_script_dir,
    '../code/bess-v1'))
__bessctl_dir = os.path.join(__bess_dir, 'bessctl')
__bessctl_bin = os.path.join(__bessctl_dir, 'bessctl')
__bess_kmod_dir = os.path.join(__bess_dir, 'core/kmod')


# ----------- BESS --------------
def bessctl_do(command, stdout=None, stderr=None, cpu_list=None):
    """
    Run bessctl command
    """
    cmd = '{} {}'.format(__bessctl_bin, command)
    if cpu_list is not None:
        assert isinstance(cpu_list, str)
        cmd  = 'taskset -c {} {}'.format(cpu_list, cmd)
    ret = subprocess.run(cmd, shell=True, stdout=stdout, stderr=stderr)
    return ret


def setup_bess_pipeline(pipeline_config_path):
    # Make sure bessctl daemon is down
    bessctl_do('daemon stop', stderr=subprocess.PIPE)

    # Run BESS config
    ret = bessctl_do('daemon start')
    if ret.returncode != 0:
        print('failed to start bess daemon', file=sys.stderr)
        return -1
    # Run a configuration (pipeline)
    cmd = 'daemon start -- run file {}'.format(pipeline_config_path)
    ret = bessctl_do(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    print(ret.stdout.decode())
    print(ret.stderr.decode())
    return 0


def load_bess_kmod():
  cmd = './install'
  return subprocess.check_call(cmd, shell=True, cwd=__bess_kmod_dir) 


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

