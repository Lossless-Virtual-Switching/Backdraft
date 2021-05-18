#!/usr/bin/python3
import yaml
import numpy as np

# data = np.loadtxt("1M_Req_1_Concurrency.txt")
data = np.loadtxt("/tmp/ab_stats_7.txt")
# data = np.loadtxt("/tmp/ab_stats_7_core.txt")
# data = np.loadtxt("/tmp/ab_stats_2_core.txt")

ts_data = [] 
ts_data_tmp = []

for i in data:
	ts_data.append(i[0] + i[1])
	# ts_data_tmp.append((i[0] + i[1], i[1]))

ts_data = np.array(ts_data)
# ts_data_tmp = np.array(ts_data_tmp)

ts_data.sort()
# ts_data_tmp.sort()
# ts_data_tmp =  ts_data_tmp[ts_data_tmp[:, 0].argsort()]

# print(data[0])

# exit()

# data = data1 + data2
# data = np.array(data)
# data =  data[data[:, 0].argsort()]

t_data = []
# ts_data = []
# for i in data:
# 	if len(ts_data) != 0:
# 		ts_data.append(i + ts_data[-1])
# 	else: 
# 		ts_data.append(i)


#  important
# ts_data = data

sliding_wnd = 200 
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
fig, ax = plt.subplots(figsize=(4,4))
# ax.plot(ts_data[:len(t_data)], t_data, marker=next(markercycle), linewidth=3, linestyle=next(linescycle))

# np.savetxt("x_data.txt", ts_data)
# np.savetxt("y_data.txt", t_data)

# ts_data = np.loadtxt("x_data.txt")
# t_data = np.loadtxt("y_data.txt")
# Here we draw the figure, I'm picking the first column only
# ts_data = np.array(ts_data)
# ax.plot(ts_data[:len(t_data)][:, 0], t_data, linewidth=2, linestyle=next(linescycle))
ax.plot(ts_data[:len(t_data)], t_data, linewidth=2, linestyle=next(linescycle))

# limiting the x axis
# ax.set_xlim((ts_data[0] + 11000000, ts_data[0] + 11010000))
# ax.set_xlim((ts_data[0] + 11004000, ts_data[0] + 11005000))

# limiting the y axis
# ax.set_ylim((0.5, 1.3))

# setting the tick value and locations
#locs, labels = plt.xticks()            # Get locations and labels
# ax.set_xticks(locs)
# ax.set_xticklabels([0, 200, 400, 600, 800, '1K'])
# ax.set_xticklabels([0, 2, 4, 6, 8, 10])

# setting x and y labels
# ax.set(xlabel='time (ns)', ylabel='Throughput (Mbps)')
# ax.set(xlabel='time (ms)', ylabel='Throughput (Mbps)')
ax.set(xlabel='time (ns)', ylabel='Throughput (Mbps)')

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
# ax.plot(ts_data_tmp[:,0], ts_data_tmp[:,1])
# ax.set_xlim((ts_data_tmp[0][0] + 11004000, ts_data_tmp[0][0] + 11005000))
# ax.set_xlim((ts_data_tmp[0][0] + 11000000, ts_data_tmp[0][0] + 11010000))
# ax.set_ylim((0, 500))
# 
# ax.set(xlabel='time (s)', ylabel='Latency (u)')
# ax.grid()
# 
# fig.savefig("latency.png")
