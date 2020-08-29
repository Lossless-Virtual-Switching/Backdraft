# RUN TAS
sudo ./tas/tas \
	--dpdk-extra=-w --dpdk-extra=81:00.1 \
	--fp-no-xsumoffload \
	--fp-no-autoscale \
	--fp-no-ints \
	--ip-addr=192.168.1.2 \
	--cc=dctcp-win \
	--fp-cores-max=1 \
	--fp-command-data-queu

# RUN MICRO RPC MODIFIED
# Server
LD_PRELOAD=lib/libtas_interpose.so \
	../tas-benchmark/micro_rpc_modified/echoserver_linux 1234 1 foo 100 64
# Client
LD_PRELOAD=lib/libtas_interpose.so \
	../tas-benchmark/micro_rpc_modified/testclient_linux \
	1 192.168.1.3:1234 1 foo client_log.txt 500 64 1

