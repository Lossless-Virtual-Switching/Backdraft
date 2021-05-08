import os
import sys
import math
from matplotlib import pyplot as plt
import numpy as np
# from statsmodels.distributions.empirical_distribution import ECDF


def ecdf(x, n):
    x = sorted(x)
    def _ecdf(v):
        # side='right' because we want Pr(x <= v)
        return (np.searchsorted(x, v, side='right') + 1) / n
    return _ecdf


def plot(data_file, fig, ax, label):
    y = []
    k = 0
    max_ = 0
    with open(data_file) as f:
        for i, line in enumerate(f):
            value = float(line.replace(',',''))
            y.append(value)
            if value > max_:
                max_ = value
            k += 1

    x = list(range(0, math.ceil(max_)))
    y = ecdf(y, k)(x)
    ax.plot(x, y, label=label)



# print('data loaded (%d)' % k)
fig = plt.figure()
ax = fig.add_subplot(1, 1, 1)
ax.set_xlabel('Throughput (Mbps)')
ax.set_ylabel('ECDF')
yax = ax.get_yaxis()
yax.grid(True, linestyle="dotted")
# Ticks
# time = [0, 30, 60, 90, 120]
# time = [t * 1000 for t in time]
# ax.set_xticks(time)

# ax.set_ylim(0.98)

argv = sys.argv
argc = len(argv)
if argc < 2:
    sys.exit(1)

for i in range(1, argc):
    path = argv[i]
    label = os.path.basename(path)
    plot(path, fig, ax, label)
# plot('./with_slow_recv/tmp.txt', fig, ax, 'slow')
# plot('./without_slow_recv/tmp.txt', fig, ax, 'no slow')

plt.legend()
filename = 'cdffig.jpg'
fig.savefig(filename, dpi=300)
print('file saved at', filename)

