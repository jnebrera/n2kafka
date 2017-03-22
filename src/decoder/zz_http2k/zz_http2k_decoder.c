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
#include <pthread.h>
#include <stdint.h>
#include <string.h>

static const char RB_HTTP2K_CONFIG_KEY[] = "zz_http2k_config";
static const char RB_TOPICS_KEY[] = "topics";

enum warning_times_pos {
	LAST_WARNING_TIME__QUEUE_FULL,
	LAST_WARNING_TIME__MSG_SIZE_TOO_LARGE,
	LAST_WARNING_TIME__UNKNOWN_PARTITION,
	LAST_WARNING_TIME__UNKNOWN_TOPIC,
	LAST_WARNING_TIME__END
};

struct zz_opaque {
#ifndef NDEBUG
#define ZZ_OPAQUE_MAGIC 0x0B0A3A1C0B0A3A1CL
	uint64_t magic;
#endif

	struct zz_config *zz_config;

	pthread_mutex_t produce_error_last_time_mutex[LAST_WARNING_TIME__END];
	time_t produce_error_last_time[LAST_WARNING_TIME__END];
};

#ifdef ZZ_OPAQUE_MAGIC
static void assert_zz_opaque(struct zz_opaque *zz_opaque) {
	assert(zz_opaque);
	assert(ZZ_OPAQUE_MAGIC == zz_opaque->magic);
}
#else
#define assert_zz_opaque(zz_opaque) (void)zz_opaque
#endif

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

static int
parse_topic_list_config(json_t *config, struct topics_db *new_topics_db) {
	const char *key;
	json_t *value;
	json_error_t jerr;
	json_t *topic_list = NULL;
	int pass = 0;

	assert(config);

	const int json_unpack_rc = json_unpack_ex(
			config, &jerr, 0, "{s:o}", RB_TOPICS_KEY, &topic_list);

	if (0 != json_unpack_rc) {
		rdlog(LOG_ERR, "%s", jerr.text);
		return json_unpack_rc;
	}

	if (!json_is_object(topic_list)) {
		rdlog(LOG_ERR, "%s is not an object", RB_TOPICS_KEY);
		return -1;
	}

	const size_t array_size = json_object_size(topic_list);
	if (0 == array_size) {
		rdlog(LOG_ERR, "%s has no childs", RB_TOPICS_KEY);
		return -1;
	}

	json_object_foreach(topic_list, key, value) {
		const char *topic_name = key;
		rd_kafka_topic_t *rkt = NULL;

		if (!json_is_object(value)) {
			if (pass == 0) {
				rdlog(LOG_ERR,
				      "Topic %s is not an object. Discarding.",
				      topic_name);
			}
			continue;
		}

		rd_kafka_topic_conf_t *my_rkt_conf = rd_kafka_topic_conf_dup(
				global_config.kafka_topic_conf);
		if (NULL == my_rkt_conf) {
			rdlog(LOG_ERR,
			      "Couldn't topic_conf_dup in topic %s",
			      topic_name);
			continue;
		}

		rkt = rd_kafka_topic_new(
				global_config.rk, topic_name, my_rkt_conf);
		if (NULL == rkt) {
			char buf[BUFSIZ];
			strerror_r(errno, buf, sizeof(buf));
			rdlog(LOG_ERR,
			      "Can't create topic %s: %s",
			      topic_name,
			      buf);
			rd_kafka_topic_conf_destroy(my_rkt_conf);
			continue;
		}

		topics_db_add(new_topics_db, rkt);
	}

	return 0;
}

int zz_opaque_creator(json_t *config __attribute__((unused)), void **_opaque) {
	size_t i;

	assert(_opaque);

	struct zz_opaque *opaque = (*_opaque) = calloc(1, sizeof(*opaque));
	if (NULL == opaque) {
		rdlog(LOG_ERR, "Can't alloc RB_HTTP2K opaque (out of memory?)");
		return -1;
	}

#ifdef ZZ_OPAQUE_MAGIC
	opaque->magic = ZZ_OPAQUE_MAGIC;
#endif

	for (i = 0; i < RD_ARRAYSIZE(opaque->produce_error_last_time_mutex);
	     ++i) {
		pthread_mutex_init(&opaque->produce_error_last_time_mutex[i],
				   NULL);
	}

	/// @TODO move global_config to static allocated buffer
	opaque->zz_config = &global_config.rb;

	return 0;
}

int zz_opaque_reload(json_t *config, void *opaque) {
	/* Do nothing, since this decoder does not save anything per-listener
	   information */
	(void)config;
	(void)opaque;
	return 0;
}

int zz_decoder_reload(void *vzz_config, const json_t *config) {
	int rc = 0;
	struct zz_config *zz_config = vzz_config;
	struct topics_db *topics_db = NULL;

	assert(zz_config);
	assert_zz_config(zz_config);
	assert(config);

	json_t *my_config = json_deep_copy(config);
	if (unlikely(my_config == NULL)) {
		rdlog(LOG_ERR, "Couldn't deep_copy config (out of memory?)");
		return -1;
	}

	topics_db = topics_db_new();
	const int topic_list_rc = parse_topic_list_config(my_config, topics_db);
	if (topic_list_rc != 0) {
		rc = -1;
		goto err;
	}

	pthread_rwlock_wrlock(&zz_config->database.rwlock);
	swap_ptrs(topics_db, zz_config->database.topics_db);
	pthread_rwlock_unlock(&zz_config->database.rwlock);

err:

	if (topics_db) {
		topics_db_done(topics_db);
	}

	json_decref(my_config);

	return rc;
}

void zz_opaque_done(void *_opaque) {
	assert(_opaque);

	struct zz_opaque *opaque = _opaque;
	assert_zz_opaque(opaque);

	free(opaque);
}

int parse_zz_config(void *vconfig, const struct json_t *config) {
	struct zz_config *zz_config = vconfig;

	assert(vconfig);
	assert(config);

	if (only_stdout_output()) {
		rdlog(LOG_ERR,
		      "Can't use zz_http2k decoder if not kafka "
		      "brokers configured.");
		return -1;
	}

	rd_kafka_conf_t *rk_conf = rd_kafka_conf_dup(global_config.kafka_conf);
	if (NULL == rk_conf) {
		rdlog(LOG_CRIT, "Couldn't dup conf (out of memory?)");
		goto rk_conf_dup_err;
	}

#ifdef ZZ_CONFIG_MAGIC
	zz_config->magic = ZZ_CONFIG_MAGIC;
#endif // ZZ_CONFIG_MAGIC
	const int init_db_rc = init_zz_database(&zz_config->database);
	if (init_db_rc != 0) {
		rdlog(LOG_ERR, "Couldn't init zz_database");
		return -1;
	}

	/// @TODO error treatment
	const int decoder_reload_rc = zz_decoder_reload(zz_config, config);
	if (decoder_reload_rc != 0) {
		goto reload_err;
	}

	return 0;

reload_err:
	free_valid_zz_database(&zz_config->database);
/// @TODO: Can't say if we have consumed it!
// rd_kafka_conf_destroy(rk_conf);
rk_conf_dup_err:
	return -1;
}

/** Produce a batch of messages
	@param topic Topic handler
	@param msgs Messages to send
	@param len Length of msgs */
static void produce_or_free(struct zz_opaque *opaque,
			    struct topic_s *topic,
			    rd_kafka_message_t *msgs,
			    int len) {
	assert(topic);
	assert(msgs);
	static const time_t alert_threshold = 5 * 60;

	rd_kafka_topic_t *rkt = topics_db_get_rdkafka_topic(topic);

	const int produce_ret = rd_kafka_produce_batch(rkt,
						       RD_KAFKA_PARTITION_UA,
						       RD_KAFKA_MSG_F_FREE,
						       msgs,
						       len);

	if (produce_ret != len) {
		int i;
		for (i = 0; i < len; ++i) {
			if (msgs[i].err != RD_KAFKA_RESP_ERR_NO_ERROR) {
				time_t last_warning_time = 0;
				int warn = 0;
				const size_t last_warning_time_pos =
						kafka_error_to_warning_time_pos(
								msgs[i].err);

				if (last_warning_time_pos <
				    LAST_WARNING_TIME__END) {
					const time_t curr_time = time(NULL);
					pthread_mutex_lock(
							&opaque->produce_error_last_time_mutex
									 [last_warning_time_pos]);
					last_warning_time =
							opaque->produce_error_last_time
									[last_warning_time_pos];
					if (difftime(curr_time,
						     last_warning_time) >
					    alert_threshold) {
						opaque->produce_error_last_time
								[last_warning_time_pos] =
								curr_time;
						warn = 1;
					}
					pthread_mutex_unlock(
							&opaque->produce_error_last_time_mutex
									 [last_warning_time_pos]);
				}

				if (warn) {
					/* If no alert threshold established or
					 * last alert is too old */
					rdlog(LOG_ERR,
					      "Can't produce to topic %s: %s",
					      rd_kafka_topic_name(rkt),
					      rd_kafka_err2str(msgs[i].err));
				}

				free(msgs[i].payload);
			}
		}
	}
}

/*
 *  MAIN ENTRY POINT
 */

static void process_zz_buffer(const char *buffer,
			      size_t bsize,
			      const keyval_list_t *msg_vars,
			      struct zz_opaque *opaque,
			      struct zz_session **sessionp) {

	// json_error_t err;
	// struct zz_database *db = &opaque->zz_config->database;
	// /* @TODO const */ json_t *uuid_enrichment_entry = NULL;
	// char *ret = NULL;
	struct zz_session *session = NULL;
	const unsigned char *in_iterator = (const unsigned char *)buffer;

	assert(sessionp);

	if (NULL == *sessionp) {
		/* First call */
		*sessionp = new_zz_session(opaque->zz_config, msg_vars);
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

void zz_decode(char *buffer,
	       size_t buf_size,
	       const keyval_list_t *list,
	       void *_listener_callback_opaque,
	       void **vsessionp) {
	struct zz_opaque *zz_opaque = _listener_callback_opaque;
	struct zz_session **sessionp = (struct zz_session **)vsessionp;
	/// Helper pointer to simulate streaming behavior
	struct zz_session *my_session = NULL;

	assert_zz_opaque(zz_opaque);

	if (NULL == vsessionp) {
		// Simulate an active
		sessionp = &my_session;
	}

	process_zz_buffer(buffer, buf_size, list, zz_opaque, sessionp);

	if (buffer) {
		/* It was not the last call, designed to free session */
		const size_t n_messages =
				rd_kafka_msg_q_size(&(*sessionp)->msg_queue);
		rd_kafka_message_t msgs[n_messages];
		rd_kafka_msg_q_dump(&(*sessionp)->msg_queue, msgs);

		if ((*sessionp)->topic_handler) {
			produce_or_free(zz_opaque,
					(*sessionp)->topic_handler,
					msgs,
					n_messages);
		}
	}

	if (NULL == vsessionp) {
		// Simulate last call that will free my_session
		process_zz_buffer(NULL, 0, list, zz_opaque, sessionp);
	}
}

void zz_decoder_done(void *vzz_config) {
	struct zz_config *zz_config = vzz_config;
	assert_zz_config(zz_config);

	free_valid_zz_database(&zz_config->database);
}
