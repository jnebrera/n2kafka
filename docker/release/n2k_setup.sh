#!/usr/bin/env sh

readonly OUT_FILE=config.json

# Assign default value if not value
function zz_var {
	eval "local readonly currval=\"\$$1\""
	if [ -z "${currval}" ]; then
		value="$(printf "%s" "$2" | sed 's%"%\\"%g')"
		eval "export $1=\"$value\""
	fi
}

#
# ZZ variables
#

zz_var DECODER zz_http2k
zz_var N2K_NTHREADS 3
zz_var KAFKA_BROKERS kafka
zz_var LISTENER_PORT 7980
zz_var DEFAULT_TOPIC ''

if [ ! -z DEFAULT_TOPIC ]; then
	export topic_config=",\"topic\": \"${DEFAULT_TOPIC}\""
fi

#
# All RDKAFKA_ vars will be passed to librdkafka as-is
#

envsubst < ${OUT_FILE}.env > ${OUT_FILE}

exec ./n2kafka ${OUT_FILE}
