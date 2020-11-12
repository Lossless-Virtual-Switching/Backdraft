import sys
import subprocess

sys.path.insert(0, '../')
from bkdrft_common import *
from tas_containers import run_dummy_app


# Kill anything running
bessctl_do('daemon stop', stdout=subprocess.PIPE,
        stderr=subprocess.PIPE) # this may show an error it is nothing

subprocess.run('sudo pkill dummy_app', shell=True)


# Run BESS daemon
print('start BESS daemon')
ret = bessctl_do('daemon start')
if ret.returncode != 0:
    print('failed to start bess daemon', file=sys.stderr)
    sys.exit(-1)

# Run a configuration (pipeline)
file_path = 'test_pipeline.bess'
ret = bessctl_do('daemon start -- run file {}'.format(file_path))

dummy_proc_cost = 0
dummy_target_ip_list = []
count_queue = 8
cdq = 0
d_cpu = [24]
i = 0
d_prefix = 'exp_dummy_{}'.format(i)
vdev = 'virtio_user{},path=/tmp/test_dummy_{}.sock,queues={}'
vdev = vdev.format(i, i+1, count_queue)
d_cdq = 'cdq' if cdq else '-'
conf = {
        'cpu': d_cpu[i],
        'prefix': d_prefix,
        'vdev': vdev,
        'count_queue': count_queue,
        'cdq': d_cdq,
        'process_cost': dummy_proc_cost,
        'ips': dummy_target_ip_list,
        }
run_dummy_app(conf)
