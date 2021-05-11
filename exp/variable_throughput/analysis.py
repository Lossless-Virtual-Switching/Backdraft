#!/usr/bin/python3

import numpy as np
import sys
import csv
import argparse



# data = np.loadtxt("1M_Req_1_Concurrency.txt")
# data = np.loadtxt("/tmp/ab_stats_7.txt")
def main(args):
    ts_data = [] 
    
    try:
        if args.trace_type == "NGINX":
            for j in trace_files:
                path_to_trace = j
                data = np.loadtxt(path_to_trace)
                size = 79
                if j == 3:
                   size = 237 
                print(size)
                for i in data:
                    ts_data.append((i[0] + i[1], size))
        else:
            for j in trace_files:
                path_to_trace = j
                # data = np.loadtxt(path_to_trace, delimiter=" ", usecols=(1,4))
                with open(path_to_trace, 'r') as f:
                    reader = csv.reader(f, delimiter=' ')
                    for i in reader:
                        # print(i[1], int(i[4][i[4].find("(") + 1: i[4].find(")")]))
                        ts_data.append((float(i[1]) * 1000000, int(i[4][i[4].find("(") + 1: i[4].find(")")])))
    
    except Exception as e:
        print("Error: ", e)
        exit(0)
    
    ts_data = np.array(ts_data)
    
    # ts_data.sort()
    
    if TRACE_TYPE == "MEMCACHED":
        ts_data =  ts_data[ts_data[:, 0].argsort()]
    
    t_data = []
    
    sliding_wnd = args.wnd_size
    
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
    
    t_data = np.array(t_data)
    
    
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
    # f = ts_data[0]
    # for i in range(1, len(ts_data)):
    #     ts_data[i][0] = ts_data[i][0] - f[0]
    # f[0] = 0
    # print(t_data)
    # print(ts_data)
    
    print(ts_data[0])
    # np.savetxt(args.x_file_path, ts_data[:,0])
    # np.savetxt(args.y_file_path, t_data)
    np.savetxt(args.res_dir + "/x_data.txt", ts_data[:,0])
    np.savetxt(args.res_dir + "/y_data.txt")

if __name__ == "__main__":
    
    parser = argparse.ArgumentParser()
    parser.add_argument("wnd_size", help="size of the window for analysis", type=int)
    parser.add_argument("trace_type", help="trace_type should be MEMCACHED or NGINX")
    parser.add_argument("dir_res", help="result directory")
    # parser.add_argument("--x_file_path", help="x data file", default="x_data.txt")
    # parser.add_argument("--y_file_path", help="y data file", default="y_data.txt")
    parser.add_argument("trace_files", nargs='+', help="Trace files, pass as much as you want")

    args = parser.parse_args()
    # window_size = args.wnd_size
    print(args)
    print(args.trace_files)

    main(args)
    exit(0)
