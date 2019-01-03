/*
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
** Copyright (C) 2018-2019, Wizzie S.L.
** Author: Eugenio Perez <eupm90@gmail.com>
**
** This file is part of n2kafka.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU Affero General Public License as
** published by the Free Software Foundation, either version 3 of the
** License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Affero General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "dumb.h"

#include "util/kafka.h"
#include "util/pair.h"
#include "util/util.h"

#include <librd/rdlog.h>
#include <librdkafka/rdkafka.h>

#include <string.h>
#include <syslog.h>

static enum decoder_callback_err dumb_decode(const char *buffer,
					     size_t buf_size,
					     const keyval_list_t *keyval,
					     void *listener_callback_opaque,
					     const char **response,
					     size_t *response_size,
					     void *sessionp) {
	(void)keyval;
	(void)sessionp;
	(void)listener_callback_opaque;

	rd_kafka_topic_t *rkt =
			new_rkt_global_config(default_topic_name(), NULL);
	if (unlikely(NULL == rkt)) {
		static const char *null_rkt_err =
				"No topic specified in config file";
		*response = null_rkt_err;
		*response_size = strlen(*response);
		rdlog(LOG_ERR, "Couldn't produce message: %s", *response);
		return DECODER_CALLBACK_UNKNOWN_TOPIC;
	}

	const int produce_ret = rd_kafka_produce(rkt,
						 RD_KAFKA_PARTITION_UA,
						 RD_KAFKA_MSG_F_COPY,
						 const_cast(buffer),
						 buf_size,
						 NULL /* key */,
						 0 /* key size */,
						 NULL /* opaque */);

	rd_kafka_resp_err_t kafka_error_code = RD_KAFKA_RESP_ERR_NO_ERROR;
	if (unlikely(produce_ret != 0)) {
		kafka_error_code = rd_kafka_last_error();
		*response = rd_kafka_err2str(kafka_error_code);
		*response_size = strlen(*response);
		rdlog(LOG_ERR, "Couldn't produce message: %s", *response);
	}

	rd_kafka_topic_destroy(rkt);

	switch (kafka_error_code) {
	case RD_KAFKA_RESP_ERR_NO_ERROR:
		return DECODER_CALLBACK_OK;
	case RD_KAFKA_RESP_ERR__QUEUE_FULL:
		return DECODER_CALLBACK_BUFFER_FULL;
	case RD_KAFKA_RESP_ERR_MSG_SIZE_TOO_LARGE:
		return DECODER_CALLBACK_UNKNOWN_TOPIC;
	case RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION:
		return DECODER_CALLBACK_UNKNOWN_PARTITION;
	case RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC:
		return DECODER_CALLBACK_MSG_TOO_LARGE;
	default:
		return DECODER_CALLBACK_GENERIC_ERROR;
	};
}

static const char *dumb_decoder_name() {
	return "";
}

const struct n2k_decoder dumb_decoder = {
		.name = dumb_decoder_name,
		.callback = dumb_decode,
};
