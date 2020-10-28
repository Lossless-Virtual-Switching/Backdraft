ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif

# Default target, can be overridden by command line or environment
RTE_TARGET ?= x86_64-native-linuxapp-gcc

# These should be defined before include... because the path will change
MKFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
CURRENT_DIR := $(dir $(MKFILE_PATH))

include $(RTE_SDK)/mk/rte.vars.mk

EXTRA_CFLAGS =  -Wall -Werror -I $(CURRENT_DIR)/include/
LIB = libbd_vport.a
SRCS-y := vport.c

include $(RTE_SDK)/mk/rte.extlib.mk
