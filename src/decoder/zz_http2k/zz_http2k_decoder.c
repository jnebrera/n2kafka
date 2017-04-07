/*
**
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
** Author: Eugenio Perez <eupm90@gmail.com>
** All rights reserved.
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

#include "zz_http2k_decoder.h"
#include "zz_database.h"
#include "zz_http2k_parser.h"

#include "engine/global_config.h"

#include "util/kafka.h"
#include "util/kafka_message_list.h"
#include "util/rb_json.h"
#include "util/rb_mac.h"
#include "util/rb_time.h"
#include "util/topic_database.h"
#include "util/util.h"

#include <assert.h>
#include <errno.h>
#include <jansson.h>
#include <librd/rd.h>
#include <librd/rdlog.h>
#include <librd/rdmem.h>
#include <librdkafka/rdkafka.h>
#include <stdint.h>
#include <string.h>

static struct zz_database zz_database = {NULL};

static enum warning_times_pos
kafka_error_to_warning_time_pos(rd_kafka_resp_err_t err) {
	switch (err) {
	case RD_KAFKA_RESP_ERR__QUEUE_FULL:
		return LAST_WARNING_TIME__QUEUE_FULL;
	case RD_KAFKA_RESP_ERR_MSG_SIZE_TOO_LARGE:
		return LAST_WARNING_TIME__MSG_SIZE_TOO_LARGE;
	case RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION:
		return LAST_WARNING_TIME__UNKNOWN_PARTITION;
	case RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC:
		return LAST_WARNING_TIME__UNKNOWN_TOPIC;
	default:
		return LAST_WARNING_TIME__END;
	};
}

static int zz_decoder_init(const struct json_t *config) {
	(void)config;
	if (only_stdout_output()) {
		rdlog(LOG_ERR,
		      "Can't use zz_http2k decoder if not kafka "
		      "brokers configured.");
		return -1;
	}

	const int init_db_rc = init_zz_database(&zz_database);
	if (init_db_rc != 0) {
		rdlog(LOG_ERR, "Couldn't init zz_database");
		return -1;
	}

	return 0;
}

/** Produce a batch of messages
	@param topic Topic handler
	@param msgs Messages to send
	@param len Length of msgs */
static void produce_or_free(struct zz_session *opaque,
			    struct topic_s *topic,
			    rd_kafka_message_t *msgs,
			    int len) {
	assert(topic);
	assert(msgs);
	static const time_t alert_threshold = 5 * 60;

	rd_kafka_topic_t *rkt = topics_db_get_rdkafka_topic(topic);

	int msgs_ok = rd_kafka_produce_batch(rkt,
					     RD_KAFKA_PARTITION_UA,
					     RD_KAFKA_MSG_F_FREE,
					     msgs,
					     len);

	if (likely(msgs_ok == len)) {
		// all OK!
		return;
	}

	int i;
	for (i = 0; i < len && msgs_ok < len; ++i) {
		int warn = 1;
		if (msgs[i].err == RD_KAFKA_RESP_ERR_NO_ERROR) {
			continue;
		}

		msgs_ok++;
		const size_t last_warning_time_pos =
				kafka_error_to_warning_time_pos(msgs[i].err);

		if (last_warning_time_pos <= LAST_WARNING_TIME__END) {
			const time_t last_warning_time =
					opaque->produce_error_last_time
							[last_warning_time_pos];
			const time_t now = time(NULL);
			if (difftime(now, last_warning_time) <
			    alert_threshold) {
				warn = 0;
			} else {
				opaque->produce_error_last_time
						[last_warning_time_pos] = now;
			}
		}

		if (warn) {
			rdlog(LOG_ERR,
			      "Can't produce to topic %s: %s",
			      rd_kafka_topic_name(rkt),
			      rd_kafka_err2str(msgs[i].err));
		}

		free(msgs[i].payload);
	}
}

/*
 *  MAIN ENTRY POINT
 */

static void process_zz_buffer(const char *buffer,
			      size_t bsize,
			      const keyval_list_t *msg_vars,
			      struct zz_session **sessionp) {

	struct zz_session *session = NULL;
	const unsigned char *in_iterator = (const unsigned char *)buffer;

	assert(sessionp);

	if (NULL == *sessionp) {
		/* First call */
		*sessionp = new_zz_session(&zz_database, msg_vars);
		if (NULL == *sessionp) {
			return;
		}
	} else if (0 == bsize) {
		/* Last call, need to free session */
		free_zz_session(*sessionp);
		*sessionp = NULL;
		return;
	}

	session = *sessionp;

	yajl_status stat = yajl_parse(session->handler, in_iterator, bsize);

	if (stat != yajl_status_ok) {
		/// @TODO improve this!
		unsigned char *str = yajl_get_error(
				session->handler, 1, in_iterator, bsize);
		fprintf(stderr, "%s", (const char *)str);
		yajl_free_error(session->handler, str);
	}
}

static void zz_decode(char *buffer,
		      size_t buf_size,
		      const keyval_list_t *list,
		      void *listener_opaque,
		      void **vsessionp) {
	(void)listener_opaque;
	struct zz_session **sessionp = (struct zz_session **)vsessionp;
	/// Helper pointer to simulate streaming behavior
	struct zz_session *my_session = NULL;

	if (NULL == vsessionp) {
		// Simulate an active
		sessionp = &my_session;
	}

	process_zz_buffer(buffer, buf_size, list, sessionp);

	if (buffer) {
		/* It was not the last call, designed to free session */
		const size_t n_messages =
				rd_kafka_msg_q_size(&(*sessionp)->msg_queue);
		rd_kafka_message_t msgs[n_messages];
		rd_kafka_msg_q_dump(&(*sessionp)->msg_queue, msgs);

		if ((*sessionp)->topic_handler) {
			produce_or_free((*sessionp),
					(*sessionp)->topic_handler,
					msgs,
					n_messages);
		}
	}

	if (NULL == vsessionp) {
		// Simulate last call that will free my_session
		process_zz_buffer(NULL, 0, list, sessionp);
	}
}

static void zz_decoder_done() {
	free_valid_zz_database(&zz_database);
}

static const char *zz_name() {
	return "zz_http2k";
}

static const char *zz_config_token() {
	return "zz_http2k_config";
}

static int zz_flags() {
	return DECODER_F_SUPPORT_STREAMING;
}

const struct n2k_decoder zz_decoder = {
		.name = zz_name,
		.config_parameter = zz_config_token,

		.init = zz_decoder_init,
		.done = zz_decoder_done,

		.callback = zz_decode,

		.flags = zz_flags,
};
