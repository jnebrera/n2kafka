THIS_SRCS := \
	zz_http2k_decoder.c \
	zz_database.c \
	uuid_database.c \
	zz_http2k_parser.c \
	zz_http2k_sensors_database.c \
	zz_http2k_sync_thread.c \
	zz_http2k_curl_handler.c \
	zz_http2k_organizations_database.c \
	tommyds/tommyhash.c \
	tommyds/tommyhashdyn.c \
	tommyds/tommylist.c \

SRCS := $(SRCS) $(addprefix $(CURRENT_N2KAFKA_DIR),$(THIS_SRCS))
