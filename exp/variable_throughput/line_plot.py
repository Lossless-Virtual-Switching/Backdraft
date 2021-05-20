#!/usr/bin/python3

import yaml
import matplotlib
import matplotlib.pyplot as plt
import itertools
import numpy as np
import sys

class Map(dict):
    def __init__(self, *args, **kwargs):
        super(Map, self).__init__(*args, **kwargs)
        for arg in args:
            if isinstance(arg, dict):
                for k, v in arg.items():
                    self[k] = v

        if kwargs:
            for k, v in kwargs.items():
                self[k] = v

    def __getattr__(self, attr):
        return self.get(attr)

    def __setattr__(self, key, value):
        self.__setitem__(key, value)

    def __setitem__(self, key, value):
        super(Map, self).__setitem__(key, value)
        self.__dict__.update({key: value})

    def __delattr__(self, item):
        self.__delitem__(item)

    def __delitem__(self, key):
        super(Map, self).__delitem__(key)
        del self.__dict__[key]


def load_file_from_ts(path, start, end):
    """
    only load part of the data which is
    relevant to the timestamp
    """
    data = []
    start_index = 0
    end_index = 0
    with open(path) as f:
        begin = float(f.readline())
        start = begin + start
        end = begin + end
        print(start, 'to', end)
        f.seek(0)
        for i, line in enumerate(f):
            cur = float(line)
            if cur >= start:
                start_index = i
                break
        data.append(cur)
        for i, line in enumerate(f):
            cur = float(line)
            if cur > end:
                end_index = start_index + i + 1
                break
            data.append(cur)
    return np.array(data), start_index, end_index


def load_file_from_line(path, start_line, end_line):
    """
    Load data from given lines
    """
    print('from line', start_line, 'to', end_line)
    data = []
    with open(path) as f:
        for i, line in enumerate(f):
            if i < start_line:
                continue
            if i >= end_line:
                break
            data.append(float(line))
    return np.array(data)


def draw(config):
    # Load data
    if config.x_limit:
        # Only load part of the file which is relevant
        x_axis_data, file_start_index, file_end_index = load_file_from_ts(
                config.x_axis_data, config.x_limit[0], config.x_limit[1])
        y_axis_data = load_file_from_line(config.y_axis_data,
                file_start_index, file_end_index)
    else:
        # Load all the file
        x_axis_data = np.loadtxt(config.x_axis_data)
        y_axis_data = np.loadtxt(config.y_axis_data)

    # Some preparation that are not all necessary
    master_linestyles = ['-', '--', '-.', ':']
    master_markers = ['o', 'D', 'v', '^', '<', '>', 's', 'p', '*', '+', 'x']
    my_colors = ['tab:blue', 'tab:red']

    linescycle = itertools.cycle(master_linestyles)
    markercycle = itertools.cycle(master_markers)

    # Getting the fig and ax
    fig, ax = plt.subplots(figsize=config.fig_size)

    min_dimension = min(len(x_axis_data), len(y_axis_data))
    if config.dimension_size:
        min_dimension = config.dimension_size

    ax.plot(x_axis_data[:min_dimension], y_axis_data[:min_dimension],
            linewidth=2, linestyle=next(linescycle))

    # limiting the x axis
    if config.x_limit:
        # ax.set_xlim(ts_data[0] + 11004000, ts_data[0] + 11005000))
        # print(config.x_limit)
        # ax.set_xlim((x_axis_data[0] + config.x_limit[0], x_axis_data[0] + config.x_limit[1]))
        ax.set_xlim((x_axis_data[0], x_axis_data[-1]))
        # ax.set_xlim((x_axis_data[0] + config.x_limit[0], x_axis_data[-1]))

    if config.y_limit:
        ax.set_ylim(config.y_limit)

    # setting the tick value and locations
    arr = []
    if config.x_tick_labels:
        # import math
        locs, labels = plt.xticks()            # Get locations and labels
        ax.set_xticks(locs)
        # for i in locs:
        #     if len(arr):
        #         last = arr[-1] + 50 / len(locs)
        #     else:
        #         last = 50 / len(locs)
        #     arr.append(math.floor(last))
        # print(arr)
        ax.set_xticklabels(config.x_axis_ticks)


    # setting x and y labels
    ax.set(xlabel=config.x_label, ylabel=config.y_label)

    # grid stuff
    if config.grid:
        yax = ax.get_yaxis()
        yax.grid(True, linestyle="dotted")

    # Let's save some space
    plt.tight_layout()

    # Don't you want to save this nice figure?
    fig.savefig(config.fig_name + ".pdf", dpi=config.dpi)
    ##########################################

if __name__ == '__main__':
    yaml_data = None
    config_file = sys.argv[1]
    with open(config_file, 'r') as steam:
        try:
            # print(yaml.safe_load(stream))
            yaml_data = yaml.safe_load(steam)
        except yaml.YAMLError as exc:
            print(exc)

    draw(Map(yaml_data[0]))