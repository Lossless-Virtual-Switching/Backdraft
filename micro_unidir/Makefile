TARGETS = unidir_linux unidir_ll unidir_mtcp

TAS_CODE?=${HOME}/tas/code

CFLAGS = -std=gnu99 -g -Wall -O3 -march=native \
	-I$(TAS_CODE)/include -I$(TAS_CODE)/lib/tas/include
LDFLAGS =-m64 -g -O3 -pthread  -march=native -no-pie
LDLIBS = -lrt

include ../common/Makefile.mtcp

all: unidir_linux unidir_ll
all-sockets: unidir_linux
all-mtcp: unidir_mtcp
all-ll: unidir_ll


unidir_linux: unidir.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

unidir_ll: unidir_ll.o $(TAS_CODE)/lib/libtas.so
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

unidir_mtcp: unidir.mtcp.o
	$(CC) $(LDFLAGS) $(MTCP_LDFLAGS) -o $@ $^ $(LDLIBS) $(MTCP_LDLIBS)


%.mtcp.o: %.c
	$(CC) $(CFLAGS) $(MTCP_CFLAGS) -c -o $@ $<


clean:
	rm -f *.o ${TARGETS}

.PHONY: all all-sockets all-mtcp clean
