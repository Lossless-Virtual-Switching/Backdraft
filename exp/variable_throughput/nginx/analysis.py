#!/usr/bin/python3

import numpy as np
import sys

if len(sys.argv) < 2:
    print("Error: Invalid parameters: Path to trace not existed")
    print("usage: ./program <path_to_trace>")
    exit(0)

path_to_trace = sys.argv[1]
# data = np.loadtxt("1M_Req_1_Concurrency.txt")
# data = np.loadtxt("/tmp/ab_stats_7.txt")
try:
    data = np.loadtxt(sys.argv[1])
except Exception as e:
    print("Error: ", e)
    exit(0)
# data = np.loadtxt("/tmp/ab_stats_2_core.txt")

ts_data = [] 

for i in data:
	ts_data.append(i[0] + i[1])

ts_data = np.array(ts_data)

ts_data.sort()

# data =  data[data[:, 0].argsort()]

t_data = []

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

np.savetxt("x_data.txt", ts_data)
np.savetxt("y_data.txt", t_data)



