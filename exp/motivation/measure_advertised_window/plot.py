import matplotlib.pyplot as plt
import csv

data = '/tmp/clean.txt'
book = {}
with open(data) as f:
    reader = csv.reader(f)
    for line in reader:
        line = map(float, line)
        ts, src, dst, w = line
        fid = (src, dst)
        if fid not in book:
            book[fid] = {'x': [], 'y': []}
        book[fid]['x'].append(ts)
        book[fid]['y'].append(w)


figsize = (4,4)
fig = plt.figure(figsize=figsize)
ax = fig.add_subplot(1,1,1)
for i, fid in enumerate(book):
    if book[fid]['x'][-1] - book[fid]['x'][0] < 5:
        continue
    label = f'flow-{i}'
    if i == 6:
        ax.plot(book[fid]['x'], book[fid]['y'], label=label)
    print(i)
    if i > 5:
        break

ax.set_xlabel('Time (sec)')
ax.set_ylabel('Advertised Window (bytes)')
plt.tight_layout()
plt.legend()
fig.savefig('/tmp/test.pdf', dpi=300)
