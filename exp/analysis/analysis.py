import numpy as np
import sys

# 42_rx.csv  43_rx.csv  BG_tx.csv  FG_tx.csv

data = np.loadtxt(sys.argv[2], skiprows=500)
rx_high = np.loadtxt(sys.argv[1] + '/42_rx.csv', delimiter=',', usecols=(3, 6), skiprows=1)
rx_low = np.loadtxt(sys.argv[1] + '/43_rx.csv', delimiter=',', usecols=(3, 6), skiprows=1)
tx_low = np.loadtxt(sys.argv[1] + '/BG_tx.csv', delimiter=',', usecols=(3, 6), skiprows=1)
tx_high = np.loadtxt(sys.argv[1] + '/FG_tx.csv', delimiter=',' ,usecols=(3, 6), skiprows=1)


per99 = np.percentile(data, 99)
per999 = np.percentile(data, 99.9)
per9999 = np.percentile(data, 99.99)

low_prio_dropped_packets = tx_low[-1][1] - rx_low[-1][1]
high_prio_dropped_packets = tx_high[-1][1] - rx_high[-1][1]

low_prio_rate_rx = rx_low[-1][0]
low_prio_rate_tx = tx_low[-1][0]
high_prio_rate_rx = rx_high[-1][0]
high_prio_rate_tx = tx_high[-1][0]

with open('stats.txt', 'w') as f:
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
