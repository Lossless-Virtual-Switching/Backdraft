#!/usr/bin/python3
"""
============= Backdraft ================
This script setups a node in the short-long-flow experiment.
There are three types of nodes in this experiment. 1) Server,
2) Long Flow Clients, and 3) Short Flow Clients.
"""


import os
import sys
import argparse
import subprocess
import json
from pprint import pprint
from cluster_config_parse_utils import *

sys.path.insert(0, '../')
from bkdrft_common import *
from tas_containers import *


def get_socket_with_name(name):
        """
        """
        # k = 0
        # socket_name_temp  = '/tmp/bkdrft/{0}{1}.sock'
        socket_name_temp  = '/tmp/{0}.sock'
        # socket = socket_name_temp.format(name, k)
        socket = socket_name_temp.format(name)
        # while os.path.exists(socket):
        #     k += 1
        #     socket = socket_name_temp.format(name, k)
        socket = os.path.abspath(os.path.join(cur_script_dir, socket))
        return socket


def get_flow_config():
        """
        Return a dictionary with configuration information
        for short and long flows. 

        args is a global variable
        """
        flow_conf_file = args.flow_config
        txt = ''
        with open(flow_conf_file) as f:
            txt = f.read()
        conf = json.loads(txt)
        return conf


def setup_container(node_name: str, instance_number: int, config: dict):
        """
        Setup a client node
        yaml config of a node having a client instance:

        ...
        node-name:
          ip: ... (required)
          mac: ... (required)
          instances:
            - port: ... (required)
              type: client
              cpus: comma seperate cpu ids, (default: "2,4,6")
              flow_config_name: what is the name to look in the flow config file (default: "default_client")
              tas_cores: fp_max_cores parameter of the tas (default: 2)
            - port: ... (another instance)
              .
              .
              .

         flow config example (in flow_config.json file):

         {
          ....
          "flow_config_name": {
            "message_per_sec": value,
            "message_size": value,
            "flow_num_msg": value, // how many messages before a flow finishes
            "flow_per_destination": value,
            "count_thread": value, // how many threads to be used for the app
          }
         }

        """

        node_config = config[node_name]
        instance = node_config['instances'][instance_number]
        instance_type = instance['type']
        ip = node_config['ip']  # current node ip
        if 'ip' in instance:
                ip = instance['ip']
        port = instance.get('port', '8432')

        # destination ip, port tuples
        if instance_type == 'client':
                dsts = instance.get('destinations', [])
                if not dsts:
                        print(('node ({}) instance ({}) is a client but has '
                               'no destination! skipping this instance.')
                               .format(node_name, instance_number))
                        return
                ips = []
                for name in dsts:
                        dst_name, dst_instance = name.split('_')
                        dst_instance = int(dst_instance)
                        dst_config = config[dst_name]  # machine config
                        dst_instance_config = dst_config['instances'][dst_instance]
                        dst_ip = dst_config['ip']
                        if 'ip' in dst_instance_config:
                                dst_ip = dst_instance_config['ip']
                        dst_port = dst_instance_config['port']
                        ips.append((dst_ip, dst_port))
        else:
                ips = [('10.0.0.100', '4927')] # this address is not going to affect the server

        cpu=instance.get('cpus', '2,4,6')
        cpus=len(cpu.split(','))

        container_name = 'tas_{}_{}'.format(instance_type, instance_number)
        socket = get_socket_with_name(container_name)


        default_conf_name = 'default_{}'.format(instance_type)
        flow_conf_name = instance.get('flow_config_name', default_conf_name)
        flow_conf = get_flow_config()[flow_conf_name]
        # flow_duration = flow_conf['flow_duration']
        flow_duration = 0  # this feature is not working so just disable it
        message_size = flow_conf['message_size']
        if instance_type == 'client':
                flow_num_msg = flow_conf['flow_num_msg']
                message_per_sec = flow_conf['message_per_sec']
                flow_per_destination = flow_conf['flow_per_destination']
                count_flow = len(ips) * flow_per_destination
        elif instance_type == 'server':
                flow_num_msg = 0
                message_per_sec = -1
                count_flow = flow_conf['max_flow']
        count_thread = flow_conf.get('count_thread', 1)

        tas_cores = instance.get('tas_cores', 2)

        config = {
                'name': container_name,
                'type': instance_type,
                'image': container_image_name,
                'cpu': cpu,
                'socket': socket,
                'ip': ip,
                'prefix': container_name,
                'cpus': cpus,
                'port': port,
                'count_flow': count_flow,
                'ips': ips,
                'flow_duration': flow_duration, # (value is in ms), 0 = no limit
                'message_per_sec': message_per_sec, # -1 = not limited
                'tas_cores': tas_cores,
                'tas_queues': pipeline_config['count_queue'],
                'cdq': int(pipeline_config['cdq']),
                'message_size': message_size,
                'flow_num_msg': flow_num_msg,
                'count_threads': count_thread,
        }
        # TODO: needs better implementation for supporting different configs
        if args.app == 'unidir':
            if len(ips) > 1:
                  print('warning: unidir supports only one destination!', file=sys.stderr)
            app_params = {
                'server_ip': ips[0][0],  # unidir supports only one destination
                'threads': count_thread,
                'connections': count_flow,
                'message_size': message_size,
                'server_delay_cycles': flow_conf.get('delay_cycles', 0) # no effect on client
            }
            config.update(app_params) 
        run_container(config)
        # pprint(config)


def run_container(config):
        if args.app == 'rpc':
                spin_up_tas(config)
        elif args.app == 'unidir':
                spin_up_unidir(config)
        else:
                raise Exception('app type is unknown')


def kill_node(instances):
        # stop containers
        for i, instance in enumerate(instances):
                name = 'tas_{}_{}'.format(instance['type'], i)
                subprocess.run('sudo docker stop -t 0 {}'.format(name), shell=True, stdout=subprocess.PIPE)
                subprocess.run('sudo docker rm {}'.format(name), shell=True, stdout=subprocess.PIPE)
        # stop BESS pipeline
        bessctl_do('daemon stop')


if __name__ == '__main__':
        # global constants
        cur_script_dir = os.path.dirname(os.path.abspath(__file__))
        bessctl_dir = os.path.abspath(
                os.path.join(cur_script_dir, '../../code/bess-v1/bessctl'))
        bessctl_bin = os.path.join(bessctl_dir, 'bessctl')
        tas_spinup_script = os.path.abspath(os.path.join(cur_script_dir,
                '../../code/apps/tas_container/spin_up_tas_container.sh'))
        default_config_path = './cluster_config.yml'
        default_flow_conf_file = './flow_config.json'


        # parse arguments
        supported_app = ('rpc', 'unidir')
        parser = argparse.ArgumentParser(description='Setup a node for short-long-flow experiment')
        parser.add_argument('-config',
                help="path to cluster config file (default: {})".format(default_config_path),
                default=default_config_path)
        parser.add_argument('-flow_config',
                help="path to flow config file (default: {})".format(default_flow_conf_file),
                default=default_flow_conf_file)
        parser.add_argument('-bessonly', help='only setup BESS pipeline',
                action='store_true', default=False)
        parser.add_argument('-kill', help='kill previous running instances and exit',
                action='store_true', default=False)
        parser.add_argument('-app', choices=supported_app, default='rpc',
                help='which app should be used for the experiment')
        args = parser.parse_args()

        # define the image to be used in exp.
        if args.app == 'rpc':
                container_image_name = 'tas_container'
        elif args.app == 'unidir':
                container_image_name = 'tas_unidir'

        # read config file 
        full_config = get_full_config(args.config)
        node_name, info = get_current_node_info(args.config)
        if node_name is None:
                print('Configuration for current node not found (mac address did not match any node)')
                sys.exit(-1)
        instances = info['instances']

        # kill previous instances
        if args.kill:
                kill_node(instances)
                sys.exit(0)

        # bring up BESS pipeline
        server_pipeline = os.path.join(cur_script_dir, 'general_pipeline.bess')
        setup_bess_pipeline(server_pipeline)
        if args.bessonly:
                print('only BESS has been setuped')
                sys.exit(0)

        # read pipeline config
        pipeline_config = json.load(open('.pipeline_config.json'))
        print('pipeline config:')
        pprint(pipeline_config)
        print('')
        
        # setup TAS instances
        for i, instance in enumerate(instances): 
                print('setting up instance:')
                pprint(instance)
                print('')
                if instance['type'] in ('client', 'server'):
                        setup_container(node_name=node_name, instance_number=i, config=full_config)
                else:
                        print('instance type ({}) is not defined'.format(instance['type']))

        print('Node has been setup successfully')

