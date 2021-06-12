from trex_stl_lib.api import *
import argparse


# Tunable example 
#
#trex>profile -f stl/multi_flow.py
#
#Profile Information:
#
#
#General Information:
#Filename:         stl/multi_flow.py
#Stream count:          1024      
#
#Specific Information:
#Type:             Python Module
#Tunables:         ['stream_count = 1', 'direction = 0', 'packet_len = 64']
#
#trex>start -f stl/multi_flow.py -t  packet_len=128 --port 0
#

src_port_base = 8000 
dst_port_base = 5123
src_ip = "192.168.1.2"
dst_ip = "192.168.1.1"

class STLS1(object):
    def create_stream (self, packet_len, stream_id):
        sport = src_port_base + stream_id
        dport = dst_port_base + stream_id
        base_pkt = Ether()/IP(src=src_ip,dst=dst_ip)/UDP(dport=dport,sport=sport)
        base_pkt_len = len(base_pkt)
        base_pkt /= 'x' * max(0, packet_len - base_pkt_len)
        return STLStream(
                packet = STLPktBuilder(pkt = base_pkt),
                mode = STLTXCont())

    def get_streams (self, tunables, **kwargs):
        parser = argparse.ArgumentParser(description='Argparser for {}'.format(os.path.basename(__file__)), 
                                         formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        parser.add_argument('--packet_len',
                            type=int,
                            default=64,
                            help="The packets length in the stream")
        parser.add_argument('--stream_count',
                            type=int,
                            default=1024,
                            help="The number of streams")
        args = parser.parse_args(tunables)
        packets = [self.create_stream(args.packet_len - 4, i) for i in range(args.stream_count)]
        return packets 


# dynamic load - used for trex console or simulator
def register():
    return STLS1()



