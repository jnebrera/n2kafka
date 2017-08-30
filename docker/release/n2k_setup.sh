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

zz_var N2K_NTHREADS 3
zz_var KAFKA_BROKERS kafka
zz_var HTTP2K_PORT 7980

#
# All RDKAFKA_ vars will be passed to librdkafka as-is
#

# Override librdkafka defaults
zz_var RDKAFKA_SOCKET_KEEPALIVE_ENABLE true
zz_var RDKAFKA_MESSAGE_SEND_MAX_RETRIES 0
zz_var RDKAFKA_API_VERSION_REQUEST true

export kafka_opts=""

# Read all librdkafka envs, chop first RDKAFKA, and change '_' for '.'
while IFS='=' read rdkafka_key rdkafka_val; do
	kafka_opts="$kafka_opts\"$rdkafka_key\":\"$rdkafka_val\","
done <<EOF
$(env | grep '^RDKAFKA_' | tr 'A-Z_' 'a-z.')
EOF

# Delete last comma & pretty printing
kafka_opts=$(printf '%s' "${kafka_opts%,}" | sed 's%,%,\n  %g')

envsubst < ${OUT_FILE}.env > ${OUT_FILE}

exec ./n2kafka ${OUT_FILE}
