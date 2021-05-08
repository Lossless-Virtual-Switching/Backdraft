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

def draw(config):

    # Load data
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
    # ax.plot(ts_data[:len(t_data)], t_data, marker=next(markercycle), linewidth=3, linestyle=next(linescycle))
    
    # Here we draw the figure, I'm picking the first column only
    # ts_data = np.array(ts_data)
    # ax.plot(ts_data[:len(t_data)][:, 0], t_data, linewidth=2, linestyle=next(linescycle))
    min_dimension = min(len(x_axis_data), len(y_axis_data))
    if config.dimension_size:
        min_dimension = config.dimension_size

    ax.plot(x_axis_data[:min_dimension], y_axis_data[:min_dimension], linewidth=2, linestyle=next(linescycle))
    
    # limiting the x axis
    # ax.set_xlim((ts_data[0] + 11000000, ts_data[0] + 11010000))
    if config.x_limit:
        # ax.set_xlim(ts_data[0] + 11004000, ts_data[0] + 11005000))
        # print(config.x_limit)
        # ax.set_xlim(eval(config.x_limit))
        ax.set_xlim((x_axis_data[0] + config.x_limit[0], x_axis_data[0] + config.x_limit[1]))

    if config.y_limit:
        ax.set_ylim(eval(config.y_limit))
        
    # setting the tick value and locations
    if config.x_tick_labels:
        locs, labels = plt.xticks()            # Get locations and labels
        ax.set_xticks(locs)
        ax.set_xticklabels(config.x_tick_labels)
    
    # setting x and y labels
    # ax.set(xlabel='time (ms)', ylabel='Throughput (Mbps)')
    ax.set(xlabel=config.x_label, ylabel=config.y_label)
    
    # grid stuff
    if config.grid:
        yax = ax.get_yaxis()
        yax.grid(True, linestyle="dotted")
    
    # Let's save some space
    plt.tight_layout()
    
    # Don't you want to save this nice figure?
    fig.savefig(config.fig_name, dpi=config.dpi)
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
