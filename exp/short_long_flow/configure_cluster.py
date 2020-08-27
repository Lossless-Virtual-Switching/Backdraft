#!/usr/bin/python3
import os
import sys
import yaml

print(
"""

THIS SCRIPT IS NOT USABLE ANY MORE
THE CONFIG FILE STRUCTURE HAS CHANGED.

"""
)

sys.exit(1)




config_file = './cluster_config.txt'
info_file = './cluster_info.txt'

# TODO: take these info from program aguments
count_server = 2
count_long_flow = 3
count_short_flow = 1
n_regular_graph = 2 # each client is connected to on server
min_count_nodes = count_server + count_long_flow + count_short_flow

if count_server < n_regular_graph:
	print('number of server nodes are not sufficient for a client to have {} connections'.format(n_regular_graph), file=sys.stderr) 
	sys.exit(1)

types = ['server' for i in range(count_server)] + \
	['long-flow' for i in range(count_long_flow)] + \
	['short-flow' for i in range(count_short_flow)]

ports = {
	'server': 1234,
	'long-flow': 5678,
	'short-flow': 1456,
}


if __name__ == '__main__':
	if os.path.exists(config_file):
		print('another config file already exists. replacing is not a good behavior', file=sys.stderr)
		sys.exit(1)
	
	roles =  {'server': [], 'long-flow': [], 'short-flow': []}
	config = dict()
	state = 0
	count_nodes = 0
	with open(info_file) as f:
		for line in f:
			line.strip()
			if state == 0:
				name = line.split('.')[0]
				state += 1
			elif state == 1:
				raw = line.split()
				mac = raw[4]
				ip = raw[6][5:]
				state += 1
			elif state == 2:
				node_type = types[count_nodes]
				port = ports[node_type]
				config[name] = {
					'ip': ip,
					'mac': mac,
					'type': node_type,
					'port': port,
				}
				roles[node_type].append(name)
				count_nodes += 1
				state = 0
	
	if count_nodes < min_count_nodes:
		print('There are not enough servers described in info file.', file=sys.stderr)
		sys.exit(1)

	connections = dict()
	for client in (roles['long-flow'] + roles['short-flow']):
		servers = roles['server'][:n_regular_graph]
		connections[client] = servers
	config['connections'] =connections
	
	# create yaml file
	with open(config_file, 'w') as f:
		yaml.dump(config, f)
				
					
