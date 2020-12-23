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

# Get include and lib directory for DPDK
# ifneq ($(wildcard $(RTE_SDK)/$(RTE_TARGET)/*),)
#   DPDK_INC_DIR := $(RTE_SDK)/$(RTE_TARGET)/include
#   DPDK_LIB_DIR := $(RTE_SDK)/$(RTE_TARGET)/build/lib
# else ifneq ($(wildcard $(RTE_SDK)/build/*),)
#   # if the user didn't do "make install" for DPDK
#   DPDK_INC_DIR := $(RTE_SDK)/build/include
#   DPDK_LIB_DIR := $(RTE_SDK)/build/lib
# else ifneq ($(MAKECMDGOALS),clean)
#   $(error DPDK is not available. Make sure $(abspath $(RTE_SDK)) is available and built)
# endif

# $(info $(DPDK_INC_DIR) \n $(DPDK_LIB_DIR))

# Add directories for searching header files
CDirSearch = -isystem $(DPDK_INC_DIR) \
	     -iquote $(CURRENT_DIR)/utils/include \
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

#  -lgflags
LIBS = -Wl,-non_shared \
       -Wl,--whole-archive -l$(DPDK_LIB) -Wl,--no-whole-archive \
       -Wl,-call_shared \
       -lbd_vport -lbkdrft -lprotobuf-c \
       -lpcap -lnuma \
       -lpthread -ldl \
       -lmlx5 -lmlx4 -libverbs -lm

# binary name
APP = dpdk_netperf

# all source are stored in SRCS-y
SRCS := dpdk_netperf.c
SRCS += utils/zipf.c
SRCS += utils/percentile.c
# SRCS-y += data_structure/f_linklist.c

OBJS = $(patsubst %.c, %.o,$(SRCS))

default: $(OBJS) $(APP)

%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)

$(APP): $(OBJS)
	mkdir -p $(CURRENT_DIR)/build
	$(CC) -o $(CURRENT_DIR)/build/$@ $^ $(LDFLAGS) $(LIBS)

clean:
	rm $(APP) *.o utils/*.o

.PHONY: clean

