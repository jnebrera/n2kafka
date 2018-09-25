THIS_SRCS := \
	http.c \
	responses.c

SRCS += $(addprefix $(CURRENT_N2KAFKA_DIR), $(THIS_SRCS))
THIS_SRCS :=
