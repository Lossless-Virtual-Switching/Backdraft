from test_utils import *


class BessMeasurePPSTest(BessModuleTestCase):

    def test_run_measure_pps_simple(self):
        m = MeasurePPS()
        self.run_for(m, [0], 3)
        self.assertBessAlive()


    def test_measure_pps_simple(self):
        m = MeasurePPS()
        pkt_in = get_tcp_packet(sip='22.22.22.22', dip='22.22.22.22')
        pkt_outs = self.run_module(m, 0, [pkt_in], [0])
        self.assertEquals(len(pkt_outs[0]), 1)
        self.assertSamePackets(pkt_outs[0][0], pkt_in)

        pkt_outs = self.run_module(m, 0, [pkt_in, pkt_in, pkt_in], [0])
        self.assertEquals(len(pkt_outs[0]), 3)


    def test_measure_pps_stats_summary(self):
        udp = get_udp_packet(sip='172.12.0.3', dip='127.12.0.4')
        m = MeasurePPS()
        Source() -> Rewrite(templates=[bytes(udp)]) -> \
            Bypass(cycles_per_batch=100) -> m -> Sink()
        self.bess.resume_all()
        # Run for 3 seconds to warm up ...
        time.sleep(3.5)
        self.bess.pause_all()
        self.assertBessAlive()
        stats = self.bess.run_module_command(m.name, 'get_summary', 'EmptyArg', {})
        self.assertEquals(len(stats.records), 3)


suite = unittest.TestLoader().loadTestsFromTestCase(BessMeasurePPSTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
