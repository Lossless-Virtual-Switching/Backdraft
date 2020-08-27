#!/usr/bin/python3
"""
This script is used to create a csv file from the 
accuired tas_container logs.
The latency information are extracted from last line
of all *.txt files in the given directory, and they are
stored in the output file.
"""
import argparse
import os


def slow_down_rate_from_file_name(fname):
   res = float(fname.split('.')[0].split('_')[-1])
   return res


def get_percentiles_from_line(line):
    raw = line.split()
    def _value(index):
        element = raw[index]
        res = element.split('=')[1]
        if res == '-1' and index > 5:
            res = _value(index - 2)
        return float(res)
    res = {}
    res[50.0] = _value(5)
    res[90.0] = _value(7)
    res[95.0] = _value(9)
    res[99.0] = _value(11)
    res[99.9] = _value(13)
    res[99.99] = _value(15)
    return res


def get_file_avg_percentiles(file):
    sum_stats = {} 
    count = 0
    # read the lines and sum the values
    for i, line in enumerate(file):
        if i < 30:
            # skip first 10 lines
            continue
        if i % 3 == 0:
            # line having total value
            continue
        if i % 3 == 2:
            # line having stats for slow server
            continue
        perc = get_percentiles_from_line(line)
        for p, value in perc.items():
            if p not in sum_stats:
                sum_stats[p] = 0
            sum_stats[p] += value
        count += 1

    # divide results by the count
    res = {}
    for p, value in sum_stats.items():
        res[p] = value / count

    return res


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('path', help='path to the directory of log files')
    parser.add_argument('output', help='path to output file')
    args = parser.parse_args()

    # find the target files
    curdir = os.getcwd()
    os.chdir(args.path)
    all_items = os.listdir()
    txt_files = filter(lambda x: x.endswith('.txt'), all_items)

    # gather last lines
    data = []
    slow_down_values = []
    for fname in txt_files:
        with open(fname) as file:
            # calculate average percentile values for this file
            # avg_stats = get_file_avg_percentiles(file)

            slow_by = slow_down_rate_from_file_name(fname)

            # data is for how much of slow_down 
            res = (slow_by, avg_stats)
            data.append(res)
            slow_down_values.append(slow_by)

    # sort lines by slow_down (ascending order)
    data.sort(key=lambda x: x[0])
    slow_down_values.sort()

    # group date by percentile (in ascending slow_by order)
    percentile_map = {}
    for slow_by, percentiles in data:
        for p, value in percentiles.items():
            if p not in percentile_map:
                percentile_map[p] = []
            percentile_map[p].append(value)

    # back to directory where started
    os.chdir(curdir)

    # writing output file
    with open(args.output, 'w') as file:
        # write a auto-generated notice
        msg = ('# ===============================================\n'
                '# This file is auto generated from `tas_containers`'
                'log files stored at:\n'
                '#  {}\n'
                '# TCP Slow Receiver Experiment: Latency Results\n'
                '#\n'
                '# Header (slow down by): {}\n'
                '# ===============================================\n'
                ).format(os.getcwd(), str(slow_down_values))
        file.write(msg)

        for p, values in percentile_map.items():
            file.write('# @{}\n'.format(p))
            str_values = map(str, values)
            line = ','.join(str_values) + '\n'
            file.write(line)

