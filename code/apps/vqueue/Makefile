ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif

# Default target, can be overridden by command line or environment
RTE_TARGET ?= x86_64-native-linuxapp-gcc

# These should be defined before include... because the path will change
MKFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
CURRENT_DIR := $(dir $(MKFILE_PATH))

include $(RTE_SDK)/mk/rte.vars.mk

# binary name
APP = vport_test_app

# all source are stored in SRCS-y
SRCS-y := src/main.c
SRCS-y += src/server.c
SRCS-y += src/client.c
SRCS-y += src/zipf.c
SRCS-y += src/vport/vport.c

CFLAGS += $(WERROR_FLAGS)

# workaround for a gcc bug with noreturn attribute
# http://gcc.gnu.org/bugzilla/show_bug.cgi?id=12603
ifeq ($(CONFIG_RTE_TOOLCHAIN_GCC),y)
CFLAGS_main.o += -Wno-return-type
endif

EXTRA_CFLAGS += -Ofast -g -Wfatal-errors -Wno-format-nonliteral \
								-I $(CURRENT_DIR)/include/

include $(RTE_SDK)/mk/rte.extapp.mk

