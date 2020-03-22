TOPOLOGY = SWINGOUT_BALANCED

ifeq ($(shell hostname),triangle)
TOPOLOGY = LOCAL
else
OPTFLAGS = -O2 -march=native
endif

ifeq ($(findstring BIGFISH,$(TOPOLOGY)),BIGFISH)
FREQ = 1600.0
else ifeq ($(findstring bigfish,$(TOPOLOGY)),bigfish)
FREQ = 1600.0
else
FREQ = 2200.0
endif

CFLAGS = -std=gnu99 -g -Wall -Werror -I. -D$(TOPOLOGY) $(OPTFLAGS)
LDFLAGS = -pthread
LDLIBS = -lrt /home/simon/flextcp/sockets/flextcp_sockets.so
OBJS = worker.o libcollections.a
TASKS = spout.o count.o print.o rank.o

RTE_SDK = $(HOME)/dpdk/x86_64-native-linuxapp-gcc

ifeq ($(findstring DPDK,$(TOPOLOGY)),DPDK)
CFLAGS+= -I$(RTE_SDK)/include -DUSE_DPDK
LDFLAGS+= -L$(RTE_SDK)/lib/
LIBS_DPDK+= -Wl,--whole-archive -lrte_pmd_ixgbe \
            -Wl,--no-whole-archive # IXGBE driver
LIBS_DPDK+= -lrte_eal -lrte_malloc -lrte_mempool -lrte_ring # DPDK Basics
LIBS_DPDK+= -lethdev -lrte_mbuf -lrte_eal -lrte_pmd_ring              # DPDK net

LDLIBS += $(LIBS_DPDK) -lm -lpthread -lrt -ldl
endif

DPDK=1
PS=0
NETMAP=0

MTCP_BASE?=${HOME}/mtcp

# DPDK LIBRARY and HEADER
DPDK_INC=${MTCP_BASE}/dpdk/include
DPDK_LIB=${MTCP_BASE}/dpdk/lib/

# mtcp library and header 
MTCP_FLD    =${MTCP_BASE}/mtcp
MTCP_INC    =-I${MTCP_FLD}/include
MTCP_LIB    =-L${MTCP_FLD}/lib
MTCP_TARGET = ${MTCP_LIB}/libmtcp.a

PS_DIR = ../../io_engine/
PS_INC = ${PS_DIR}/include
INC = -I./include/
INC_MTCP=${MTCP_INC}
LIBS_MTCP = ${MTCP_LIB}
ifeq ($(PS),1)
INC += -I{PS_INC}
LIBS_MTCP += -lmtcp -L${PS_DIR}/lib -lps -lpthread -lnuma -lrt
endif

ifeq ($(NETMAP),1)
LIBS_MTCP += -lmtcp -lpthread -lnuma -lrt
endif

# CFLAGS for DPDK-related compilation
INC += ${MTCP_INC}
ifeq ($(DPDK),1)
DPDK_MACHINE_FLAGS = $(shell cat  ${MTCP_BASE}/dpdk/include/cflags.txt)
INC_MTCP+= ${DPDK_MACHINE_FLAGS} -I${DPDK_INC} -include $(DPDK_INC)/rte_config.h
endif

LIBS += -m64 -g -O3 -pthread -lrt -march=native
ifeq ($(DPDK),1)
LIBS_MTCP += -Wl,-export-dynamic
LIBS_MTCP += -Wl,--start-group
LIBS_MTCP += -L${DPDK_LIB}
LIBS_MTCP += -Wl,-lmtcp
LIBS_MTCP += -Wl,-lnuma
LIBS_MTCP += -Wl,-lpthread
LIBS_MTCP += -Wl,-lrt
LIBS_MTCP += -Wl,-ldl
LIBS_MTCP += -Wl,--whole-archive
LIBS_MTCP += -Wl,-lrte_distributor
LIBS_MTCP += -Wl,-lrte_kni
LIBS_MTCP += -Wl,-lrte_pipeline
LIBS_MTCP += -Wl,-lrte_table
LIBS_MTCP += -Wl,-lrte_port
LIBS_MTCP += -Wl,-lrte_timer
LIBS_MTCP += -Wl,-lrte_hash
LIBS_MTCP += -Wl,-lrte_lpm
LIBS_MTCP += -Wl,-lrte_power
LIBS_MTCP += -Wl,-lrte_acl
LIBS_MTCP += -Wl,-lrte_meter
LIBS_MTCP += -Wl,-lrte_sched
LIBS_MTCP += -Wl,-lm
LIBS_MTCP += -Wl,-lrt
LIBS_MTCP += -Wl,-lrte_kvargs
LIBS_MTCP += -Wl,-lrte_mbuf
LIBS_MTCP += -Wl,-lrte_ip_frag
LIBS_MTCP += -Wl,-lethdev
LIBS_MTCP += -Wl,-lrte_mempool
LIBS_MTCP += -Wl,-lrte_ring
LIBS_MTCP += -Wl,-lrte_eal
LIBS_MTCP += -Wl,-lrte_cmdline
LIBS_MTCP += -Wl,-lrte_cfgfile
LIBS_MTCP += -Wl,-lrte_pmd_bond
LIBS_MTCP += -Wl,-lrte_pmd_vmxnet3_uio
LIBS_MTCP += -Wl,-lrte_pmd_i40e
LIBS_MTCP += -Wl,-lrte_pmd_ixgbe
LIBS_MTCP += -Wl,-lrte_pmd_e1000
LIBS_MTCP += -Wl,-lrte_pmd_ring
LIBS_MTCP += -Wl,--no-whole-archive
LIBS_MTCP += -Wl,-lrt
LIBS_MTCP += -Wl,-lm
LIBS_MTCP += -Wl,-ldl
LIBS_MTCP += -Wl,--end-group
endif

all: worker flexnic worker_mtcp

startall: all
	./startall.sh

build:
	./deploy.sh build

deploy:
	./deploy.sh | tee $(TOPOLOGY).log

eval:
	./filter.awk $(TOPOLOGY).log | ./eval.awk -v FREQ=$(FREQ)

libcollections.a: hash_table.o list.o hash.o
	$(AR) r $@ $^

libarranet.a: arranet.o inet_chksum.o
	$(AR) r $@ $^

worker: $(TASKS) $(OBJS)

flexnic: flexnic.o $(TASKS) libcollections.a

worker.o flexnic.o $(TASKS): storm.h worker.h Makefile

worker_mtcp.o: worker.c storm.h worker.h Makefile
	${CC} -DUSE_MTCP=1 -c -o $@ $< ${CFLAGS} ${INC} ${INC_MTCP}

worker_mtcp: worker_mtcp.o $(TASKS) libcollections.a
	${CC} $^ ${LIBS} ${LIBS_MTCP} -o $@

clean:
	rm -f *.o worker flexnic worker_mtcp
