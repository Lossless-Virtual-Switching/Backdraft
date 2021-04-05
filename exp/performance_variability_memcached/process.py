from math import ceil


def print_stats(ts, data, count):
    percentiles = [0.990, 0.999, 0.9999]
    data.sort()
    for p in percentiles:
        assert (p < 1.0)
        index = ceil(p * (count - 1))
        index = min(index, count - 1)
        print ('{}| @{}: {}'.format(ts, p * 100, data[index]))
    print('-' * 32)


data_file = 'latency.txt'

pre_ts = -1
data = []
count = 0
with open(data_file) as f:
    for line in f:
        ts, lat = line.split()
        ts = int(ts.split('.')[0]) 
        if ts != pre_ts:
            if count:
                print_stats(ts, data, count)
            pre_ts = ts
            data.clear()
            count = 0
        data.append(lat)
        count += 1
