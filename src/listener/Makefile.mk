THIS_SRCS := listener_api.c
SRCS += $(addprefix $(CURRENT_N2KAFKA_DIR), $(THIS_SRCS))
THIS_SRCS :=

SUBDIRS := \
	http \
	socket
include $(addprefix $(CURRENT_N2KAFKA_DIR), $(addsuffix /Makefile.mk, $(SUBDIRS)))
SUBDIRS :=
