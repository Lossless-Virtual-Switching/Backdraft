ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif

# Default target, can be overridden by command line or environment
RTE_TARGET ?= x86_64-native-linuxapp-gcc

# These should be defined before include... because the path will change
MKFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
CURRENT_DIR := $(dir $(MKFILE_PATH))

include $(RTE_SDK)/mk/rte.vars.mk

EXTRA_CFLAGS = -I $(CURRENT_DIR)/include/ \
								-I $(CURRENT_DIR)/protobuf/_compile \
								-I $(CURRENT_DIR)/../bd_vport/include \
								-Wall

LDLIBS = -L$(CURRENT_DIR)/../bd_vport/build/ -lbd_vport -lprotobuf-c

LIB = libbkdrft.a

SRCS-y := bkdrft.c
SRCS-y += bkdrft_vport.c
SRCS-y += packet.c
SRCS-y += protobuf/_compile/bkdrft_msg.pb-c.c

include $(RTE_SDK)/mk/rte.extlib.mk
