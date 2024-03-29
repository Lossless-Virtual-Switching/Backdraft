ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif

MKFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
CURRENT_DIR := $(dir $(MKFILE_PATH))

CC = gcc

# Default target, can be overridden by command line or environment
RTE_TARGET ?= x86_64-native-linuxapp-gcc
DPDK_LIB ?= dpdk

DPDK_INC_DIR := $(RTE_SDK)/build/include
DPDK_LIB_DIR := $(RTE_SDK)/build/lib

# Add directories for searching header files
CDirSearch = -isystem $(DPDK_INC_DIR) \
       -iquote $(CURRENT_DIR)/include \
       -iquote $(CURRENT_DIR)/../../lib/bd_vport/include \
       -iquote $(CURRENT_DIR)/../../lib/bkdrft/include \
       -iquote $(CURRENT_DIR)/../../lib/bkdrft/protobuf/_compile

CWarningFlags = -Werror -Wall -Wextra -Wcast-align -Wno-return-type

# -DNDEBUG
COptimize = -O3 -g

CFLAGS = -march=native $(CDirSearch) -D_GNU_SOURCE $(CWarningFlags) \
   $(COptimize)
LDFLAGS = -L$(DPDK_LIB_DIR) -Wl,-rpath=$(DPDK_LIB_DIR) \
    -L$(CURRENT_DIR)/../../lib/bd_vport/build/ \
    -L$(CURRENT_DIR)/../../lib/bkdrft/build/ \

# Check if Mellanox is installed
mellanox_search := $(shell ldconfig -v -N 2> /dev/null | grep mlx5; echo $$?)
rc := $(lastword $(mellanox_search))
# $(info shell status: $(rc))
ifeq ($(rc),0)
MELLANOX = -lmlx5 -lmlx4 -libverbs
else
$(warning "Warning: mellanox is not active")
MELLANOX =
endif

#  -lgflags
LIBS = -Wl,-non_shared \
       -Wl,--whole-archive -l$(DPDK_LIB) -Wl,--no-whole-archive \
       -Wl,-call_shared \
       -lbd_vport -lbkdrft -lprotobuf-c \
       -lpcap -lnuma \
       -lpthread -ldl \
       $(MELLANOX) -lm

# binary name
APP = udp_app

# all source are stored in SRCS-y
SRCS := main.c
SRCS += server.c
SRCS += client.c
SRCS += utils/exp.c
SRCS += utils/percentile.c
SRCS += utils/flow_rules.c
SRCS += utils/arp.c
SRCS += utils/zipf.c
SRCS += utils/exponential.c

OBJS = $(patsubst %.c, %.o,$(SRCS))

default: $(OBJS) $(APP)

%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)

$(APP): $(OBJS)
	mkdir -p $(CURRENT_DIR)/build
	$(CC) -o $(CURRENT_DIR)/build/$@ $^ $(LDFLAGS) $(LIBS)

clean:
	rm *.o utils/*.o build/$(APP)

.PHONY: clean

