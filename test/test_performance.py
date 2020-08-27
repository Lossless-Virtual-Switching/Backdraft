import os
import subprocess
from unittest import TestCase


def parse_exp_stdout(text):
    count_servers = 2
    servers = [dict()  for i in range(count_servers)]

    # for x in text.split('\n'):
        # print(x)
    lines = iter(text.split('\n'))
    for sindex in range(count_servers):
        for line in lines:
            if line.startswith('Latency [@99.9](us):'):
                servers[sindex]['latency'] = float(line.split()[-1])
                break
        # print('*')
        lines.__next__()
        summation = 0
        count = 0
        for line in lines:
            if line.startswith('=='):
                break
            val = float(line.split()[-1])
            summation += val
            count += 1
        servers[sindex]['avg_tput'] = summation / count
    return servers

 
class TestBKDRFTPerformance(TestCase):
    def test_bkdrft_no_bp_performance(self):
        curdir = os.path.dirname(__file__)
        expdir = os.path.join(curdir, '../exp/slow_receiver_exp/')
        expdir = os.path.abspath(expdir)

        exp_script = os.path.join(expdir, 'run_slow_receiver_exp.py')

        # run BKDRFT no Backpressure
        res = subprocess.run('{} bkdrft 0'.format(exp_script),
                shell='True',
                stdout=subprocess.PIPE)

        # parse stdout
        servers = parse_exp_stdout(res.stdout.decode())

        for s in servers:
            print(s['latency'], s['avg_tput'])
            self.assertTrue(s['latency'] < 50)
            self.assertTrue(s['avg_tput'] > 3000000)


