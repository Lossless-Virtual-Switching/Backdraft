import numpy as np
import sys
import os
#import argparse

#TODO: I need a class for parsing my input stuff, ohh python has something!

#if(len(sys.argv) < 5):
#    print("usage: python3 <results_dir> <latency_file_path> <background_workload_rate> <drop>")
#    exit(1)
'''    
valid_path = os.path.exist(sys.argv[1] and 
        os.path.exist(sys.argv[2]) and 
        os.path.exist(sys.argv[3]) and 
        os.path.exist(sys.argv[4])

if(not valid_path):
    exit(1)
'''

def average_throughput(exp_data):
    throughput = 0
    for i in exp_data:
        # print(i[0])
        throughput = throughput + i[0]
    # print(throughput, len(exp_data))
    return float(throughput)/len(exp_data)

data=[]
#for i in [100, 90, 80, 70, 60, 50, 40, 30, 20, 10]:
for i in range(100, 0, -2):
    file_name = "0_0_" + str(i) + "_rx.csv"
    file_path = os.path.join("/home/alireza/Documents/post-loom/exp/results", file_name)
    tmp = np.loadtxt(file_path, delimiter=',', usecols=(3,6), skiprows=1)
    data.append(tmp)
    tavg = average_throughput(tmp)
    print(tavg)

#for i in data:
#    print(average_throughput(i))




'''
rx_high = np.loadtxt(os.path.join(sys.argv[1], '42_rx.csv'), delimiter=',', usecols=(3, 6), skiprows=1)
rx_low = np.loadtxt(os.path.join(sys.argv[1], '43_rx.csv'), delimiter=',', usecols=(3, 6), skiprows=1)
tx_low = np.loadtxt(os.path.join(sys.argv[1], 'BG_tx.csv'), delimiter=',', usecols=(3, 6), skiprows=1)
tx_high = np.loadtxt(os.path.join(sys.argv[1], 'FG_tx.csv'), delimiter=',' ,usecols=(3, 6), skiprows=1)
data = np.loadtxt(sys.argv[2], skiprows=500)
brate = sys.argv[3]
drop_stat = sys.argv[4]


per99 = np.percentile(data, 99)
per999 = np.percentile(data, 99.9)
per9999 = np.percentile(data, 99.99)

low_prio_dropped_packets = tx_low[-1][1] - rx_low[-1][1]
high_prio_dropped_packets = tx_high[-1][1] - rx_high[-1][1]

low_prio_rate_rx = rx_low[-1][0]
low_prio_rate_tx = tx_low[-1][0]
high_prio_rate_rx = rx_high[-1][0]
high_prio_rate_tx = tx_high[-1][0]

with open('stats_{0}.txt'.format(brate), 'w') as f:
    f.write("99 percentile latency {0} us\n".format(per99 * 10 ** -3))
    f.write("99.9 percentile latency {0} us\n".format(per999 * 10 ** -3))
    f.write("99.99 percentile latency {0} us\n".format(per9999 * 10 ** -3))
    f.write("Max Latency {0} ms\n".format(np.max(data) * 10 ** -6))

    f.write("#lpd {0}\n".format(low_prio_dropped_packets))
    f.write("#hpd {0}\n".format(high_prio_dropped_packets))

    f.write("lprrx {0} Mpps\n".format(low_prio_rate_rx))
    f.write("hprrx {0} Mpps\n".format(high_prio_rate_rx))

    f.write("lprtx {0} Mpps\n".format(low_prio_rate_tx))
    f.write("hprtx {0} Mpps\n".format(high_prio_rate_tx))

if(0):
    with open('plot/latency_{0}.txt'.format(drop_stat), 'a') as f:
        f.write("{0}\n".format(per99 * 10 ** -3))

    with open('plot/drop_{0}.txt'.format(drop_stat), 'a') as f:
        f.write("{0}\n".format(high_prio_dropped_packets))

    with open('plot/background_rate.txt', 'a') as f:
        f.write("{0}\n".format(brate))
'''
