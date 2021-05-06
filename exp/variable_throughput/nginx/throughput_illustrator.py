#!/usr/bin/python3

import numpy as np
data = np.loadtxt("1M_Req_1_Concurrency.txt")

t_data = []

ts_data = []
for i in data:
	if len(ts_data) != 0:
		ts_data.append(i + ts_data[-1])
	else: 
		ts_data.append(i)



sliding_wnd =500
bytes_per_request = 79
counter = 0
tput = 0
window = []

cnt = 0
size = len(ts_data)
while(cnt < size):
	if len(window) > 1 and (window[-1] - window[0]) >= sliding_wnd:
		# calculate the throughput
		tput = bytes_per_request * (len(window) - 1) / sliding_wnd
		window.pop(0)
		t_data.append(tput)
		continue
	window.append(ts_data[cnt])
	cnt += 1

if window:
	tput = bytes_per_request * len(window) / sliding_wnd
	t_data.append(tput)

# This part is just verification
print(len(t_data), len(ts_data))
# print(t_data[-50:-1])
# print(t_data[0:1000])
# print(ts_data)
# print(t_data)



######################################
import matplotlib
import matplotlib.pyplot as plt
import itertools


# Some preparation that are not all necessary
master_linestyles = ['-', '--', '-.', ':']
master_markers = ['o', 'D', 'v', '^', '<', '>', 's', 'p', '*', '+', 'x']
my_colors = ['tab:blue', 'tab:red']

linescycle = itertools.cycle(master_linestyles)
markercycle = itertools.cycle(master_markers)

# Data for plotting
t = np.arange(0, len(t_data), 1)

# Getting the fig and ax
fig, ax = plt.subplots(figsize=(4,2))
# ax.plot(ts_data[:len(t_data)], t_data, marker=next(markercycle), linewidth=3, linestyle=next(linescycle))

# Here we draw the figure
ax.plot(ts_data[:len(t_data)], t_data, linewidth=3, linestyle=next(linescycle))

# limiting the x axis
ax.set_xlim((11000000, 11010000))

# limiting the y axis
ax.set_ylim((0.5, 1.3))

# setting the tick value and locations
locs, labels = plt.xticks()            # Get locations and labels
ax.set_xticks(locs)
ax.set_xticklabels([0, 2, 4, 6, 8, 10])

# setting x and y labels
ax.set(xlabel='time (ms)', ylabel='Throughput (Mbps)')

# grid stuff
yax = ax.get_yaxis()
yax.grid(True, linestyle="dotted")

# Let's save some space
plt.tight_layout()

# Don't you want to save this nice figure?
fig.savefig("throughput.png", dpi=300)
##########################################
# latency

# t = np.arange(0, len(data), 1)
# 
# fig, ax = plt.subplots()
# ax.plot(ts_data, data)
# 
# ax.set(xlabel='time (s)', ylabel='Latency (u)')
# ax.grid()
# 
# fig.savefig("latency.png")
