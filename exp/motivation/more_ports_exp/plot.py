#!/usr/bin/python3
import numpy as np
import matplotlib.pyplot as plt
from exp_result import load_result_file


font = {'family' : 'normal',
        'weight' : 'normal',
        'size'   : 20}

plt.rc('font', **font)


def label_bar(ax, rects, labels):
    for rect, label in zip(rects, labels):
        height = rect.get_height()
        ax.annotate('{}'.format(label),
                xy=(rect.get_x() + rect.get_width() / 2, height),
                xytext=(0, 3),
                textcoords="offset points",
                ha='center', va='bottom')


if __name__ == '__main__':
    prefix = ['MULTIPLE_PMD_MULTIPLE_Q_{}_results.txt'] # 'SINGLE_PMD_MULTIPLE_Q_{}_results.txt', 
    outprefix = ['multiple_pmd_mutiple_q'] # 'single_pmd_multiple_q', 

    bess_name = 'BESS'
    bkdrft_name = 'Backdraft'
    for p,o in zip(prefix, outprefix):
        results_bess = load_result_file('results/' + p.format('BESS'))
        results_bkdrft = load_result_file('results/' + p.format('BKDRFT'))

        # TODO: Sort results by excess ports (if needed)
        x = [r.total_ports for r in results_bess]
        y_bess = [r.pkt_per_sec / 1000000 for r in results_bess]
        y_bkdrft = [r.pkt_per_sec / 1000000 for r in results_bkdrft]
        xlabel = 'Number of PMDPort Queues'

        # pkt per sec
        fig = plt.figure()
        ax = fig.add_subplot(1, 1, 1)
        ax.plot(x, y_bess, label=bess_name, color='red')
        ax.plot(x, y_bkdrft, label=bkdrft_name, color='blue')
        ax.set_xlabel(xlabel)
        ax.set_ylabel('Throughput(Mpps)')
        plt.legend()
        fig.savefig(o + '_result_pkt_per_sec.pdf', bbox_inches='tight', dpi=600)

        # mean latency
        y_bess = [r.mean_latency_us for r in results_bess]
        y_bkdrft = [r.mean_latency_us for r in results_bkdrft]
        plt.cla()
        fig = plt.figure()
        ax = fig.add_subplot(1, 1, 1)
        ax.plot(x, y_bess, label=bess_name, color='red')
        ax.plot(x, y_bkdrft, label=bkdrft_name, color='blue')
        ax.set_xlabel(xlabel)
        ax.set_ylabel('Mean latency(us)')
        plt.legend()
        fig.savefig(o+'_result_mean_latency.pdf', bbox_inches='tight', dpi=600)

        # udp drop
        y_bess = [r.send_failure / 1000000 for r in results_bess]
        y_bkdrft = [r.send_failure / 1000000 for r in results_bkdrft]
        plt.cla()
        fig = plt.figure()
        ax = fig.add_subplot(1, 1, 1)
        ax.plot(x, y_bess, label=bess_name, color='red')
        ax.plot(x, y_bkdrft, label=bkdrft_name,color='blue')
        ax.set_xlabel(xlabel)
        ax.set_ylabel('Packets failed to send(Mpps)')
        plt.legend()
        fig.savefig(o+'_result_send_failure.pdf', bbox_inches='tight', dpi=600)

        # bess drop
        x_t = [i for i in range(len(x))]
        x_1 = [x_ - 0.25 for x_ in x_t]
        x_2 = [x_ + 0.25 for x_ in x_t] 
        y_bess = [r.bess_drops for r in results_bess]
        y_bkdrft = [r.bess_drops for r in results_bkdrft]
        lbl_bess = [str(r.bess_drops) for r in results_bess]
        lbl_bkdrft = [str(r.bess_drops) for r in results_bkdrft]
        plt.cla()
        fig = plt.figure()
        ax = fig.add_subplot(1, 1, 1)
        rects = ax.bar(x_1, y_bess, label=bess_name, color='#aa0000', width=0.5)
        # label_bar(ax, rects, lbl_bess)
        rects = ax.bar(x_2, y_bkdrft, label=bkdrft_name, color='#0000aa', width=0.5)
        ax.set_xticks(x_t)
        ax.set_xticklabels(x)
        # label_bar(ax, rects, lbl_bkdrft)

        # ================
        ax2 = ax.twinx()
        ax2.set_ylabel('Packets failed to send(Mpps)', fontsize=22)
        y_bess = [r.send_failure / 1000000 for r in results_bess]
        y_bkdrft = [r.send_failure / 1000000 for r in results_bkdrft]
        ax2.plot(x_t, y_bess, label=bess_name, color='red')
        ax2.plot(x_t, y_bkdrft, label=bkdrft_name,color='blue')

        ax.set_xlabel(xlabel)
        ax.set_ylabel('Packet drop in switch(pps)')
        plt.legend()
        fig.savefig(o+'_result_switch_drop.pdf', bbox_inches='tight', dpi=600)
        print(o)
