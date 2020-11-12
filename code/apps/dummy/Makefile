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

# -Werror
CWarningFlags =  -Wall -Wextra -Wcast-align -Wno-return-type

# -DNDEBUG
COptimize = -O3 -g

CFLAGS = -march=native $(CDirSearch) -D_GNU_SOURCE $(CWarningFlags) \
   $(COptimize)
LDFLAGS = -L$(DPDK_LIB_DIR) -Wl,-rpath=$(DPDK_LIB_DIR) \
    -L$(CURRENT_DIR)/../../lib/bd_vport/build/ \
    -L$(CURRENT_DIR)/../../lib/bkdrft/build/ \

#  -lgflags
$(warning "Warning: mellanox is not active")
MELLANOX = # -lmlx5 -lmlx4 -libverbs
LIBS = -Wl,-non_shared \
       -Wl,--whole-archive -l$(DPDK_LIB) -Wl,--no-whole-archive \
       -Wl,-call_shared \
       -lbd_vport -lbkdrft -lprotobuf-c \
       -lpcap -lnuma \
       -lpthread -ldl \
       $(MELLANOX) -lm

# binary name
APP = dummy_app

# all source are stored in SRCS-y
SRCS := main.c
SRCS += server.c
SRCS += utils/exp.c

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

