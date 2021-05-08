#!/usr/bin/python3

import numpy as np
import sys

if len(sys.argv) < 2:
    print("Error: Invalid parameters: Path to trace not existed")
    print("usage: ./program [<path_to_trace>]")
    exit(0)

path_to_trace = sys.argv[1]
# data = np.loadtxt("1M_Req_1_Concurrency.txt")
# data = np.loadtxt("/tmp/ab_stats_7.txt")
ts_data = [] 

try:
    for j in range(1, len(sys.argv)):
        path_to_trace = sys.argv[j]
        data = np.loadtxt(path_to_trace)
        size = 79
        if j == 2:
           size = 237 
        print(size)
        for i in data:
            ts_data.append((i[0] + i[1], size))
except Exception as e:
    print("Error: ", e)
    exit(0)
# data = np.loadtxt("/tmp/ab_stats_2_core.txt")


ts_data = np.array(ts_data)

# ts_data.sort()

ts_data =  ts_data[ts_data[:, 0].argsort()]

t_data = []

sliding_wnd = 20000000
# bytes_per_request = 79
counter = 0
tput = 0
window = []
total_bytes = ts_data[0][1]

cnt = 0
size = len(ts_data)
s_wnd_idx = 0
e_wnd_idx = 0


while(e_wnd_idx < size - 1 or e_wnd_idx != s_wnd_idx):

    if e_wnd_idx < size - 1 and ts_data[e_wnd_idx][0] - ts_data[s_wnd_idx][0] <= sliding_wnd:
        e_wnd_idx += 1
        total_bytes += ts_data[e_wnd_idx][1]
        continue


    tput = (total_bytes - ts_data[e_wnd_idx][1]) / sliding_wnd
    t_data.append(tput)

    total_bytes -= ts_data[s_wnd_idx][1]
    s_wnd_idx += 1

tput = (total_bytes/sliding_wnd)
t_data.append(tput)


print(s_wnd_idx)
print(e_wnd_idx)


#     if e_wnd_idx % 1000000 == 0:
#         print("total size: ", size, " cur index:", e_wnd_idx)
#     if e_wnd_idx - s_wnd_idx + 1 > 1 and ts_data[e_wnd_idx][0] - ts_data[s_wnd_idx][0] >= sliding_wnd:
#         # print("len window", wind_size)
#         #calculate the throughput
#         # tput = bytes_per_request * (len(window) - 1) / sliding_wnd
#         # tput = total_bytes * (e_wnd_idx - s_wnd_idx) / sliding_wnd
#         tput = (total_bytes - ts_data[e_wnd_idx][1]) / sliding_wnd
#         t_data.append(tput)
#         total_bytes -= ts_data[s_wnd_idx][1]
#         s_wnd_idx += 1
#         continue
#     total_bytes += ts_data[e_wnd_idx][1]
#     e_wnd_idx += 1
# 

# if e_wnd_idx - s_wnd_idx > 0:
#     tput = total_bytes / sliding_wnd
#     t_data.append(tput)
# 

# This part is just verification
print(len(t_data), len(ts_data))

np.savetxt("x_data.txt", ts_data[:,0])
np.savetxt("y_data_20s.txt", t_data)


