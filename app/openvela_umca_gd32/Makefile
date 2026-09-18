include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_LVX_USE_DEMO_CONTEST2026_417_UMCA_GD32_PROGNAME)
PRIORITY  = $(CONFIG_LVX_USE_DEMO_CONTEST2026_417_UMCA_GD32_PRIORITY)
STACKSIZE = $(CONFIG_LVX_USE_DEMO_CONTEST2026_417_UMCA_GD32_STACKSIZE)
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_417_UMCA_GD32)

UMCA_REPO = $(realpath $(CURDIR)/../..)
UMCA_ROOT = $(UMCA_REPO)/umca

CFLAGS += -DUMCA_PROFILE_DISCOVERY
CFLAGS += -I$(UMCA_ROOT)/include
CFLAGS += -I$(UMCA_ROOT)/src/core
CFLAGS += -I$(UMCA_ROOT)/phy/uart
CFLAGS += -I$(UMCA_ROOT)/ports/openvela
CFLAGS += -I$(UMCA_REPO)/app/umca_demo
CFLAGS += -I$(UMCA_REPO)/../packages/ai_agent/src
CFLAGS += -I$(UMCA_REPO)/../packages/ai_agent/include

VPATH += $(UMCA_ROOT)/src/base
VPATH += $(UMCA_ROOT)/src/core
VPATH += $(UMCA_ROOT)/src/pubsub
VPATH += $(UMCA_ROOT)/src/sequence
VPATH += $(UMCA_ROOT)/src/discovery
VPATH += $(UMCA_ROOT)/phy/uart
VPATH += $(UMCA_ROOT)/ports/openvela
VPATH += $(UMCA_REPO)/app/umca_demo

CSRCS += umca_byteorder.c umca_crc32c.c umca_frame.c umca_topic_id.c
CSRCS += umca_context.c umca_process.c
CSRCS += umca_topic_table.c umca_publish.c umca_dispatch.c
CSRCS += umca_sequence.c umca_discovery.c
CSRCS += umca_uart.c umca_openvela_pal.c umca_openvela_uart.c
CSRCS += umca_gd32_agent.c umca_ai_agent_registry.c demo_codec.c

MAINSRC = umca_gd32_main.c

include $(APPDIR)/Application.mk
