#!/usr/bin/python3
import argparse
from intervaltree import Interval, IntervalTree
import math
import multiprocessing
import numpy as np
import sys
import time


def sliding_window_throughput(ts_data, ticks, window_size):
    """
    Calculates throughput based on packet timestamps for
    the given ticks.
    -----------------------------------------
    ts_data: IntervalTree
    ticks: np.array, [int, ...] . This the queries
           asking throughput at specific timestamps.
           assume sorted.
    window_size: int
    returns: np.array, [throughput, ...] (Mbps)
    """
    assert(window_size > 1)
    count_ticks = ticks.shape[0]
    mbps_wnd = np.zeros(count_ticks)
    pps = np.zeros(count_ticks)
    istart = ts_data.begin()
    iend = ts_data.end()
    for i, wnd_start in enumerate(ticks):
        if wnd_start > iend:
            break
        wnd_end = wnd_start + window_size
        if wnd_end < istart:
            continue
        intervals = ts_data.overlap(wnd_start, wnd_end)
        for interval in intervals:
            s = interval[0]
            e = interval[1]
            if s >= wnd_start and e <= wnd_end:
                overlap = e - s
            elif e <= wnd_end:
                overlap = e - wnd_start
            elif s >= wnd_start:
                overlap =  wnd_end - s
            else:
                overlap = window_size
            # Size (bytes) x 8 x overlay div latency = bits in window
            tmp = interval[2] * 8 * overlap / (e - s)
            assert (overlap / (e-s)) <= 1 , '{} / {}'.format(overlap, (e-s))
            mbps_wnd[i] += tmp
            pps[i] += 1
        mbps_wnd[i] /= window_size
        pps[i] /= window_size
    return mbps_wnd, pps


# A function to pass to multiprocessing pool
def pool_fn(x):
    return sliding_window_throughput(x[0], x[1], x[2])


def sec_to_us(s):
    return int(s * 1000000)


def load_nginx(path_to_trace):
    tree = IntervalTree()
    data = np.loadtxt(path_to_trace, dtype='i8')
    offset = data[0][0]
    print("Warning: converting ns to us")
    for i in data:
        # (ts_start, ts_end, size)
        start = i[0] - offset
        end = start + i[1]
        size = int(i[2])

        # convert ns to us
        start = start // 1000
        end = end // 1000
        tree.addi(start, end, size)
    return tree


def load_mem(path_to_trace):
    tree = IntervalTree()
    with open(path_to_trace, 'r') as f:
        for line in f:
            raw = line.split()
            latency = int(math.ceil(float(raw[2])))
            end = sec_to_us(float(raw[1]))
            start = end - latency
            size = int(raw[4][7:-1])
            tree.addi(start, end, size)
    return tree


def load_tree(file_path, trace_type):
    if trace_type  == "NGINX":
        return load_nginx(file_path)
    elif trace_type == "MEMCACHED":
        return load_mem(file_path)
    else:
        raise Exception("Unknown trace type")


def general_pool_fn(x):
    """
    x[0]: function to call
    x[1] to x[n]: arguments of the function
    """
    return x[0](*x[1:])


_boot = time.monotonic()
def log(*args):
    ts = time.monotonic() - _boot
    print('{:.4f}'.format(ts), *args)


def end_to_end_tp(tree):
    tmp = 0
    for s, e, d in tree:
        tmp += d
    duration = tree.end() - tree.begin()
    tp = tmp / duration
    print(tp)
    return tp


def main(args):
    # ts_data = IntervalTree()
    COUNT_PROCESS = args.parallel
    log('USING {} Processes!'.format(COUNT_PROCESS))

    # For running in parallel first split the file and pass multiple trace files
    _args = ((load_tree, f, args.trace_type) for f in args.trace_files)
    with multiprocessing.Pool(COUNT_PROCESS) as pool:
        trees = pool.map(general_pool_fn, _args)
    count_tree = len(trees)
    log('loading data to memory finished', 'number of trees:', count_tree)

    end_to_end_tp(trees[0])

    sliding_wnd = args.wnd_size
    start_ts = min(tree.begin() for tree in trees)
    end_ts = max(tree.end() for tree in trees)
    count_ticks = int((end_ts - start_ts) // args.tick_interval)
    chunk_ticks = int(count_ticks // COUNT_PROCESS)
    log('count ticks:', count_ticks, 'chunks:',
            chunk_ticks, 'real:', COUNT_PROCESS * chunk_ticks)
    MAX_TICK_SIZE = 100000000
    tick_chunks = tuple(np.arange(start_ts + i * chunk_ticks,
            start_ts + (i+1) * chunk_ticks, dtype='i8')
            for i in range(COUNT_PROCESS))

    _args = ((tree, tick, sliding_wnd)
            for tick in tick_chunks for tree in trees)
    with multiprocessing.Pool(COUNT_PROCESS) as pool:
        res = pool.map(pool_fn, _args)
    log('calculation finished, merging...')
    merge_same_ticks = list()
    for i in range (COUNT_PROCESS):
        mbps, pps = res[i * count_tree]
        for j in range(1, count_tree):
            mbps += res[i * count_tree + j][0]
            pps += res[i * count_tree + j][1]
        merge_same_ticks.append((mbps, pps))
    log('merge same tick finished')
    t_data = np.concatenate(tuple(r[0] for r in merge_same_ticks))
    pps = np.concatenate(tuple(r[1] for r in merge_same_ticks))
    ticks = np.concatenate(tick_chunks)

    # Save results
    log('saving results at', args.dir_res)
    np.savetxt(args.dir_res + "/x_data.txt", ticks, fmt='%d')
    np.savetxt(args.dir_res + "/y_data.txt", t_data, fmt='%.2f')
    np.savetxt(args.dir_res + 'pps_data.txt', pps, fmt='%.2f')


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("wnd_size", help="size of the window for analysis", type=int)
    parser.add_argument("trace_type", help="trace_type should be MEMCACHED or NGINX")
    parser.add_argument("dir_res", help="result directory")
    # parser.add_argument("--x_file_path", help="x data file", default="x_data.txt")
    # parser.add_argument("--y_file_path", help="y data file", default="y_data.txt")
    parser.add_argument("trace_files", nargs='+', help="Trace files, pass as much as you want")
    parser.add_argument("--tick-interval", help="distance between ticks",
            type=int, default=1)
    # parser.add_argument("--skip-sorting", action='store_true', default=False,
    #         help="if data is already sorted by timestamp "
    #         "then this operation can be skipped")
    parser.add_argument("--parallel", help="Number of prallel processes",
            type=int, default=1)

    args = parser.parse_args()
    print(args.trace_files)

    main(args)
    exit(0)
