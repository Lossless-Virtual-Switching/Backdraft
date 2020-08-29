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


def bessctl_do(command, stdout=None, stderr=None):
    """
    Run bessctl command
    """
    cmd = '{} {}'.format(__bessctl_bin, command)
    ret = subprocess.run(cmd, shell=True, stdout=stdout, stderr=stderr)
    return ret


def load_bess_kmod():
  cmd = './install'
  return subprocess.check_call(cmd, shell=True, cwd=__bess_kmod_dir) 


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

