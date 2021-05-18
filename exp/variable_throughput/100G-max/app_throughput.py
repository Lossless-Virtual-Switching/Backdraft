import matplotlib.pyplot as plt

memcached = []
seastar_memcached = []
nginx = []
iperf3 = []

data = [
        # memcached,
        {
            'x': [1.05, 2.07, 3.18, 4.22, 5.27, 6.38, 7.44, 8.40, 12.30, 16.34, 33.57, 64],
            'y': [467.6, 1046.4, 1528, 1903.2, 2383.2, 2785.6, 3164, 3280, 4524, 5716.8, 9600, 11200],
            'label': 'Memcached-Linux',
            'color': 'green',
            'marker': 'o',
            },
        # seastar_memcached,
        {
            'x': [1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 32, 64],
            'y': [1320, 1622, 2259, 2746, 3165, 3628, 3936, 4257, 5498, 6409, 10394, 12126.3],
            'label': 'Memcached-DPDK',
            'color': 'blue',
            'marker': '*',
            },
        # nginx,
        {
            'x': [1,2,4,16,32,64],
            'y': [1624, 3500, 6504, 35200, 47200, 60800],
            'label': 'Nginx-Linux',
            'color': 'magenta',
            'marker': 's',
            },
        #iperf3,
        {
            'x': [1,2,3,4,5],
            'y': [3250, 60000, 76800, 84000, 89600],
            'label': 'IPerf3-Linux',
            'color': 'purple',
            'marker': 'p',
            },
        ]


def plot(fig, ax, data):
    print(data['label'])
    ax.plot(data['x'], [t/1000 for t in data['y']], label=data['label'],
        marker=data.get('marker', '*'), color=data.get('color', 'red'))
    # lastx = data['x'][-1]
    # lasty = data['y'][-1]
    # prex = data['x'][-1]
    # prey = data['y'][-1]
    # m = (lasty- prey) / (lastx-prex)
    # goal = 100000


figsize=(4,4)
fig = plt.figure(figsize=figsize)
ax = fig.add_subplot(1,1,1)
for dt in data:
    plot(fig, ax, dt)

ax.set_ylabel('Gbps')
ax.set_xlabel('# Core')
ax.legend()
ax.grid(linestyle='dotted')

fig.savefig('Figure_1.png', dpi=300)
plt.show()
