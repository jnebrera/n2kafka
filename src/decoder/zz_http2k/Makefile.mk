THIS_SRCS := \
	zz_http2k_decoder.c \
	zz_database.c \
	zz_http2k_parser.c \
	zz_http2k_parser_json.c \
	zz_http2k_parser_xml.c \

SRCS := $(SRCS) $(addprefix $(CURRENT_N2KAFKA_DIR),$(THIS_SRCS))
