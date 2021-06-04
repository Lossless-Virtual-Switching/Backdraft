#!/usr/bin/python3
import argparse
from line_plot import Map, conf_load_data
import multiprocessing
import numpy as np
import os
import yaml


x_data = None
y_data = None


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('fig_desc', help='path to fig descriptor file')
    parser.add_argument('wnd_size', help='window size', type=int)
    parser.add_argument('output_dir', help='where to store result (directory)')
    parser.add_argument('--parallel', help='number of processes',
            type=int, default=1)
    args = parser.parse_args()
    return args


def walk_data(x):
    size = y_data.shape[0]
    start, end, wnd = x
    k = start
    max_diff = 0
    max_diff_start = 0
    max_diff_end = 0
    while k < end:
        s = x_data[k]
        i_end = k
        _max = y_data[k]
        _min = y_data[k]
        while i_end < size and x_data[i_end] <= s + wnd:
            if y_data[i_end] < _min:
                _min = y_data[i_end]
            if y_data[i_end] > _max:
                _max = y_data[i_end]
            i_end += 1
        i_end -= 1
        e = x_data[i_end]
        diff = _max - _min
        if diff > max_diff:
            max_diff = diff
            max_diff_start = k
            max_diff_end = i_end
        k += 1
    return (max_diff, max_diff_start, max_diff_end)


def process(config):
    # because I am using multiple processes
    # I think not having to pass the parameters to walk_data
    # will result in better performance (maybe not)
    global x_data
    global y_data

    # Load Config
    yaml_data = None
    with open(args.fig_desc, 'r') as steam:
        try:
            yaml_data = yaml.safe_load(steam)
        except yaml.YAMLError as exc:
            print(exc)

    config = Map(yaml_data[0])

    # Load Data
    x_data, y_data = conf_load_data(config)
    size = y_data.shape[0]

    # Walk Data
    wnd = args.wnd_size
    count_proc = args.parallel
    print('Size process pool', count_proc)
    chunk_size = size // count_proc
    ranges = [(i*chunk_size, (i+1)*chunk_size, wnd)
                for i in range(count_proc)]
    with multiprocessing.Pool(count_proc) as pool:
        results = pool.map(walk_data, ranges)

    max_diff, max_diff_start, max_diff_end = 0,0,0
    for diff, start, end in results:
        if diff > max_diff:
            max_diff = diff
            max_diff_start = start
            max_diff_end = end
    print('max diff:', max_diff, 'start:', max_diff_start,
            'end:', max_diff_end)
    result_data_x = x_data[max_diff_start: max_diff_end+1]
    result_data_y = y_data[max_diff_start: max_diff_end+1]
    np.savetxt(os.path.join(args.output_dir , 'max_diff_x_data.txt'), result_data_x, fmt='%d')
    np.savetxt(os.path.join(args.output_dir, 'max_diff_y_data.txt'), result_data_y, fmt='%.2f')
    print('saved files max_diff_x_data and max_diff_y_data at',
        args.output_dir)


if __name__ == '__main__':
    args = parse_args()
    process(args)
