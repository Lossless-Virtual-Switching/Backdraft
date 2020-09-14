#!/usr/bin/python3

import os
import sys
import time
import json
import subprocess
from pprint import pformat
from cluster_config_parse_utils import get_current_node_info

sys.path.insert(0, '../')
from bkdrft_common import *


"""
This script is writen with this intend to be run by ansible.
setup_node.py can fully setup the environment of a node.
The measured values should be gathered by this script and reported.
"""


if __name__ == '__main__':
    pfc = False

    with open('.pipeline_config.json', 'r') as f:
        pipeline_config = json.load(f)

    interface = pipeline_config['interface']
    cluster_config_path = pipeline_config['cluster_config_file']
    here = os.path.dirname(__file__)
    node_name, info = get_current_node_info(cluster_config_path)
    count_instance = len(info.get('instances', []))
    if node_name is None:
        print('This node was not configured')
        sys.exit(0)

    result_path = './results'
    if not os.path.exists(os.path.join(here, result_path)):
        os.mkdir(result_path)

    output_file  = os.path.join(result_path, node_name)
    logger = Logger(open(output_file, 'w'))

    # make sure every thing is stopped
    subprocess.check_call('./setup_node.py -kill', shell=True, cwd=here)

    # setup node
    subprocess.check_call('./setup_node.py', shell=True, cwd=here)

    # warm up
    warmup_time = 10
    time.sleep(warmup_time)

    # stats: before exp
    port_stats_before = get_port_packets('pmd_port0')
    if pfc:
        pfc_stats_before = get_pfc_results(interface)

    # wait
    duration = 20
    time.sleep(duration)

    # stats: after exp
    # ===========================================
    port_stats_after = get_port_packets('pmd_port0')
    if pfc:
        pfc_stats_after = get_pfc_results(interface)

    # port delta
    delta_port_stats = delta_dict(port_stats_before, port_stats_after)
    logger.log('SW (pmd_port0) stats:')
    logger.log(pformat(delta_port_stats))
    logger.log('This is results for the port connected to NIC')
    logger.log('')

    # complete switch stats
    p = bessctl_do('show port', stdout=subprocess.PIPE)
    log = p.stdout.decode()
    logger.log(log)

    if pfc:
        # pfc delta
        delta_pfc_stats = delta_dict(pfc_stats_before, pfc_stats_after)
        logger.log('PFC statas:')
        logger.log(pformat(delta_pfc_stats))
        logger.log('')

    # overlay result
    if pipeline_config['overlay']:
        logger.log('overlay packet per second average:')
        for i in range(count_instance + 1):
            module_name = 'bkdrft_queue_out{0}'.format(i)
            cmd =  'command module {0} get_overlay_tp EmptyArg {{}}'
            cmd = cmd.format(module_name)
            res = bessctl_do(cmd, stdout=subprocess.PIPE)
            log = res.stdout.decode()
            log = log.replace('response: ', '').replace('throughput: ','').strip()
            if not log:
                logger.log('module:', module_name, 'avg: 0')
                continue
            overlay_tp = list(map(float, log.split('\n')))
            overlay_tp2 = overlay_tp[-duration:]
            avg = sum(overlay_tp2) / duration
            logger.log('module:', module_name, 'avg: {:.2f}'.format(avg))
            logger.log(overlay_tp)
        logger.log('')

    # docker logs
    for i, instance in enumerate(info.get('instances', [])):
        type_ = instance['type']
        app = instance.get('app', None)
        if app == 'udp_app':
            continue
        try:
            name = 'tas_{}_{}'.format(type_, i)
            log = get_docker_container_logs(name)
            logger.log('\n ', name)
            logger.log(log)
        except:
            pass

    # ===========================================
    # stop
    subprocess.check_call('./setup_node.py -kill', shell=True, cwd=here)

