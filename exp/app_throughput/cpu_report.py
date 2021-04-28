#!/usr/bin/python3
"""
This script needs mpstat
On Ubunutun isntall `sysstat` package
"""
import json
import math
import subprocess
import time


CLS='\033[2J\033[;H'


def main():
    cmd = 'mpstat -o JSON -N ALL -P ALL -u 1 1'
    cpu_per_line = 10
    report_interval = 2
    while True:
        try:
            res = subprocess.check_output(cmd, shell=True)
        except:
            break
        res = json.loads(res)
        res = res['sysstat']['hosts'][0]
        # print(res)
        stat = res['statistics'][0]
        count_cpu = res['number-of-cpus']
        load = {}
        for cpu in stat['cpu-load']:
            try:
                c = int(cpu['cpu'])
            except:
                continue
            v = max(100 - cpu['idle'], 0)
            load[c] = v

        count_lines = math.ceil(count_cpu / cpu_per_line)
        lines = []
        total = 0
        for l in range(count_lines):
            head = []
            val = []
            basecpu = l * cpu_per_line
            remaining = count_cpu - basecpu
            cnt_cpu_in_this_line = min(cpu_per_line, remaining)
            for i in range(cnt_cpu_in_this_line):
                cpu_num = basecpu + i
                v = load[cpu_num]
                total += v
                head.append(' cpu {:0>2d} |'.format(cpu_num))
                valstr = '{:.2f}'.format(v)
                less = 6 - len(valstr)
                valstr += ' ' * less
                if v > 49:
                    valstr = '\u001b[31m{}\u001b[0m'.format(valstr)
                elif v > 0.01:
                    valstr = '\u001b[32m{}\u001b[0m'.format(valstr)
                val.append(' {} |'.format(valstr))
            lines.append(''.join(head))
            lines.append(''.join(val))
            lines.append('')

        lines.append('total:\t{:.2f}'.format(total))

        report = '\n'.join(lines)
        print('{}{} {}\n'.format(CLS, res['date'], stat['timestamp']))
        print(report, flush=True)

        time.sleep(report_interval)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('done')
        pass
