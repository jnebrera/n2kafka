THIS_SRCS := \
	socket.c

SRCS += $(addprefix $(CURRENT_N2KAFKA_DIR), $(THIS_SRCS))
THIS_SRCS :=
