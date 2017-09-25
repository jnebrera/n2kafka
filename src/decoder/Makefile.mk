SUBDIRS := \
	dumb \
	zz_http2k \
	meraki

include $(addprefix $(CURRENT_N2KAFKA_DIR), $(addsuffix /Makefile.mk, $(SUBDIRS)))
SUBDIRS :=
