THIS_SRCS := \
	http.c \
	http_config.c \
	http_auth.c \
	responses.c \
	tls.c

SRCS += $(addprefix $(CURRENT_N2KAFKA_DIR), $(THIS_SRCS))
THIS_SRCS :=
