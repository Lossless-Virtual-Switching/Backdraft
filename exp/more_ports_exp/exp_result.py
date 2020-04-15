def load_result_file(path):
    results = []
    with open(path) as f:
        r = Result()
        for line in f:
            if line.startswith('==='):
                results.append(r)
                r = Result()
                continue
            name, val = line.split(':')
            if name == 'total ports':
                r.total_ports = int(val)
            elif name == 'excess ports':
                r.excess_ports = int(val)
            elif name == 'mean latency (us)':
                r.mean_latency_us = float(val)
            elif name == 'pkt per sec':
                r.pkt_per_sec = float(val)
            elif name == 'pkt send failure':
                r.send_failure = int(val)
            elif name == 'total pkt sent':
                r.total_pkt_send = int(val)
            elif name == 'bess_drops':
                r.bess_drops = int(val)
            elif name == 'experiment duration':
                r.exp_duration = float(val)
            else:
                print('unknown name while parsing results file:',  name, val)
    return results


class Result:
    @classmethod
    def from_netperf_stdout(cls, txt):
        r = Result()
        lines = txt.split('\n')
        for line in lines:
            if 'ran for' in line:
                raw = line.split()
                t = float(raw[2])
                r.exp_duration = t
                pkts = int(raw[5])
                r.total_pkt_send = pkts
            elif line.startswith('client reqs/s'):
                raw = line.split()
                v = float(raw[2])
                r.pkt_per_sec = v
            elif line.startswith('mean latency (us):'):
                raw = line.split()
                v = float(raw[3])
                r.mean_latency_us = v
            elif line.startswith('send failures:'):
                raw = line.split()
                v = int(raw[2])
                r.send_failure = v
        return r

    def __init__(self):
        self.excess_ports = -1
        self.total_ports = -1
        self.mean_latency_us = -1
        self.pkt_per_sec = -1
        self.send_failure = -1
        self.total_pkt_send = -1
        self.bess_drops = -1
        self.exp_duration = -1
    
    def set_excess_ports(self, count):
        self.excess_ports = count
        self.total_ports = count + 2

    def generate_report(self):
        txt = '\n'.join([
            'total ports: {}'.format(self.total_ports),
            'excess ports: {}'.format(self.excess_ports),
            'mean latency (us): {}'.format(self.mean_latency_us),
            'pkt per sec: {}'.format(self.pkt_per_sec),
            'pkt send failure: {}'.format(self.send_failure),
            'total pkt sent: {}'.format(self.total_pkt_send),
            'bess_drops: {}'.format(self.bess_drops),
            'experiment duration: {}'.format(self.exp_duration),
            '',
        ])
        return txt

    def __repr__(self):
        return '<More Ports Exp Result>'

