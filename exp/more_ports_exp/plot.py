#!/usr/bin/python3
import numpy as np
import matplotlib.pyplot as plt
from exp_result import load_result_file


def label_bar(ax, rects, labels):
    for rect, label in zip(rects, labels):
        height = rect.get_height()
        ax.annotate('{}'.format(label),
                xy=(rect.get_x() + rect.get_width() / 2, height),
                xytext=(0, 3),
                textcoords="offset points",
                ha='center', va='bottom')


if __name__ == '__main__':
    results = load_result_file('results.txt')

    # TODO: Sort results by excess ports (if needed)
    x = [r.total_ports for r in results]
    y = [r.pkt_per_sec / 1000000 for r in results]
    xlabel = 'PMDPorts'

    # pkt per sec
    fig = plt.figure()
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(x, y, label='bess')
    ax.set_xlabel(xlabel)
    ax.set_ylabel('Throughput(Mpps)')
    plt.legend()
    fig.savefig('result_pkt_per_sec.pdf', dpi=600)

    # mean latency
    y = [r.mean_latency_us for r in results]
    plt.cla()
    fig = plt.figure()
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(x, y, label='bess')
    ax.set_xlabel(xlabel)
    ax.set_ylabel('Mean latency(us)')
    plt.legend()
    fig.savefig('result_mean_latency.pdf', dpi=600)

    # udp drop
    y = [r.send_failure / 1000000 for r in results]
    plt.cla()
    fig = plt.figure()
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(x, y, label='bess')
    ax.set_xlabel(xlabel)
    ax.set_ylabel('Packets failed to send(Mpps)')
    plt.legend()
    fig.savefig('result_send_failure.pdf', dpi=600)

    # bess drop
    y = [r.bess_drops for r in results]
    lbl = [str(r.bess_drops) for r in results]
    plt.cla()
    fig = plt.figure()
    ax = fig.add_subplot(1, 1, 1)
    rects = ax.bar(x, y, label='bess', width=1)
    label_bar(ax, rects, lbl)
    ax.set_xlabel(xlabel)
    ax.set_ylabel('Packet drop in switch(pps)')
    plt.legend()
    fig.savefig('result_switch_drop.pdf', dpi=600)

