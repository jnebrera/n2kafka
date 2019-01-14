/*
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
** Copyright (C) 2018-2019, Wizzie S.L.
** Author: Eugenio Perez <eupm90@gmail.com>
**
** This file is part of n2kafka.
** Based on librdkafka example
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

/// @TODO this should not have engine/ dependences
#include "engine/global_config.h"

#include "util.h"
#include "util/kafka.h"
#include "util/kafka_message_array.h"

#include <librd/rdlog.h>
#include <librdkafka/rdkafka.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>

#define ERROR_BUFFER_SIZE 256
#define RDKAFKA_ERRSTR_SIZE ERROR_BUFFER_SIZE

typedef int (*rdkafka_stats_cb)(rd_kafka_t *rk,
				char *json,
				size_t json_len,
				void *opaque);

/// Kafka statis info
struct {
	/// Topic to send librdkafka stats.
	rd_kafka_topic_t *stats_topic;

	/// rdkafka stats callback
	rdkafka_stats_cb n2k_stats_cb;

	/// Data to add to each stats callback
	struct {
		/// Buffer to add
		char *buf;
		/// Buffer size
		size_t size;
	} append_buf;
} stats;

/// librdkafka stats_cb return code indicating that librdkafka should free stats
static const int PLEASE_FREE_LIBRDKAFKA = 0;
/// librdkafka stats_cb return code indicating that app code will free stats
static const int I_WILL_FREE_LIBRDKAFKA = 1;

/** Creates a new topic handler using global configuration
    @param topic_name Topic name
    @param partitioner Partitioner function
    @return New topic handler */
rd_kafka_topic_t *new_rkt_global_config(const char *topic_name) {
	rd_kafka_topic_t *ret =
			rd_kafka_topic_new(global_config.rk, topic_name, NULL);
	if (unlikely(NULL == ret)) {
		rdlog(LOG_ERR,
		      "Couldn't create kafka topic: %s",
		      gnu_strerror_r(errno));
	}

	return ret;
}

const char *default_topic_name() {
	return global_config.topic;
}

/**
 * Message delivery report callback.
 * Called once for each message.
 * See rdkafka.h for more information.
 */
static void msg_delivered(rd_kafka_t *rk RD_UNUSED,
			  const rd_kafka_message_t *rkmessage,
			  void *opaque RD_UNUSED) {

	if (unlikely(rkmessage->err)) {
		rblog(LOG_ERR,
		      "Message delivery failed: %s",
		      rd_kafka_err2str(rkmessage->err));
	} else {
		rblog(LOG_DEBUG,
		      "Message delivered (%zu bytes): %*.*s",
		      rkmessage->len,
		      (int)rkmessage->err,
		      (int)rkmessage->err,
		      (char *)rkmessage->payload);
	}

	if (rkmessage->_private) {
		struct kafka_message_array_internal *karray =
				kafka_message_array_internal_cast(
						rkmessage->_private);
		kafka_message_array_internal_decref(karray);
	}
}

/**
 * @brief      Send rdkafka stats to an specific topic
 *
 * @param      rk        rdkafka handler
 * @param[in]  json      The stats json
 * @param      json_len  The stats json length
 * @param      opaque    The opaque, needed for complain.
 *
 * @return     Expected in callback
 */
static int rdkafka_stats_topic_cb(rd_kafka_t *rk,
				  char *json,
				  size_t json_len,
				  void *opaque) {
	(void)rk;
	(void)opaque;

	if (NULL == stats.stats_topic) {
		// Race condition in stop_rdkafka, topic_cb can be queued
		// between rd_kafka_topic_destroy and poll() pending callbacks
		return PLEASE_FREE_LIBRDKAFKA;
	}

	const int produce_rc = rd_kafka_produce(stats.stats_topic,
						RD_KAFKA_PARTITION_UA,
						RD_KAFKA_MSG_F_FREE,
						json,
						json_len,
						NULL /* key */,
						0 /* keylen */,
						NULL /* msg_opaque */);
	if (produce_rc != 0) {
		const rd_kafka_resp_err_t rkerr = rd_kafka_last_error();
		rdlog(LOG_ERR,
		      "Can't produce stats message: %s",
		      rd_kafka_err2str(rkerr));

		return PLEASE_FREE_LIBRDKAFKA;
	}

	return I_WILL_FREE_LIBRDKAFKA;
}

/**
 * @brief      Print rdkafka stats using rdlog
 *
 * @param      rk        rdkafka handler
 * @param[in]  json      The stats json
 * @param      json_len  The stats json length
 * @param      opaque    The opaque, needed for complain.
 *
 * @return     Expected in callback
 */
static int rdkafka_stats_rdlog_cb(rd_kafka_t *rk,
				  char *json,
				  size_t json_len,
				  void *opaque) {
	(void)rk;
	(void)json_len;
	(void)opaque;

	const char *cursor = json, *next_comma = json, *comma = json;

	rdlog(LOG_INFO, "Librdkafka stats ===");
	while (next_comma) {
		// 1st iteration:
		// {blahblah,blahblah,blahblah}
		// <- cursor, comma, next_comma
		//
		// Nth iteration:
		// {blahblah,blahblah,blahblah}
		//                    <- next comma
		//           <- cursor, comma
		while (next_comma && next_comma - cursor < 1024) {
			comma = next_comma;
			const size_t processed = (size_t)(comma + 1 - json);
			next_comma = memchr(
					comma + 1, ',', json_len - processed);
		}

		if (NULL == next_comma) {
			break;
		}

		if (cursor == next_comma) {
			rdlog(LOG_ERR, "Truncated, too long output");
			break;
		}

		rdlog(LOG_INFO, "%.*s", (int)(comma + 1 - cursor), cursor);
		cursor = comma + 1;
	}

	rdlog(LOG_INFO, "%.*s", (int)(json + json_len - cursor), cursor);

	return PLEASE_FREE_LIBRDKAFKA;
}

static int rdkafka_stats_enrich_decorator_cb(rd_kafka_t *rk,
					     char *json,
					     size_t json_len,
					     void *opaque) {
	assert(stats.append_buf.size);
	assert(stats.append_buf.buf);

	const size_t new_json_len = json_len + stats.append_buf.size;
	char *new_json = realloc(json, new_json_len);
	if (unlikely(NULL == new_json)) {
		rdlog(LOG_ERR, "Can't reallocate stats buffer (OOM?)");
	} else {
		json = new_json;
		assert(json[json_len - 1] == '}');
		char *json_last_brace = &json[json_len - 1];
		*json_last_brace = ',';
		memcpy(json_last_brace + 1,
		       stats.append_buf.buf,
		       stats.append_buf.size);
		json[new_json_len - 1] = '}';
	}

	const int child_rc = stats.n2k_stats_cb(rk, json, new_json_len, opaque);

	if (new_json && child_rc == PLEASE_FREE_LIBRDKAFKA) {
		// Librdkafka can have a dangling pointer because of realloc
		free(json);
		return I_WILL_FREE_LIBRDKAFKA;
	}

	return child_rc;
}

static void print_rdkafka_conf(rd_kafka_conf_t *conf) {
	size_t dump_config_count, i;
	const char **dump_config = rd_kafka_conf_dump(conf, &dump_config_count);
	for (i = 0; i < dump_config_count; i += 2) {
		rdlog(LOG_INFO,
		      "Kafka_config[%s][%s]",
		      dump_config[i],
		      dump_config[i + 1]);
	}

	rd_kafka_conf_dump_free(dump_config, dump_config_count);
}

void init_rdkafka(n2kafka_rdkafka_conf *conf) {
	char errstr[RDKAFKA_ERRSTR_SIZE];

	if (conf->statistics.topic) {
		stats.n2k_stats_cb = rdkafka_stats_topic_cb;
	} else {
		stats.n2k_stats_cb = rdkafka_stats_rdlog_cb;
	}

	stats.append_buf.buf = conf->statistics.message_extra.buf;
	stats.append_buf.size = conf->statistics.message_extra.size;
	if (stats.append_buf.size) {
		rd_kafka_conf_set_stats_cb(conf->rk_conf,
					   rdkafka_stats_enrich_decorator_cb);
	} else {
		rd_kafka_conf_set_stats_cb(conf->rk_conf, stats.n2k_stats_cb);
	}

	print_rdkafka_conf(conf->rk_conf);
	rd_kafka_conf_set_dr_msg_cb(conf->rk_conf, msg_delivered);
	global_config.rk = rd_kafka_new(RD_KAFKA_PRODUCER,
					conf->rk_conf,
					errstr,
					RDKAFKA_ERRSTR_SIZE);

	if (!global_config.rk) {
		fatal("%% Failed to create new producer: %s", errstr);
	}

	conf->rk_conf = NULL; // consumed by rdkafka
	rd_kafka_set_log_level(global_config.rk, global_config.log_severity);

	if (conf->statistics.topic) {
		rdlog(LOG_INFO,
		      "Sending rdkafka stats to topic %s",
		      conf->statistics.topic);

		stats.stats_topic = rd_kafka_topic_new(
				global_config.rk, conf->statistics.topic, NULL);

		if (unlikely(NULL == stats.stats_topic)) {
			fatal("Can't create stats topic: %s",
			      rd_kafka_err2str(rd_kafka_last_error()));
		}
	}
}

void kafka_poll(int timeout_ms) {
	rd_kafka_poll(global_config.rk, timeout_ms);
}

void flush_kafka() {
	kafka_poll(1000);
}

void stop_rdkafka() {
	if (stats.stats_topic) {
		rd_kafka_topic_destroy(stats.stats_topic);
		stats.stats_topic = NULL;
	}
	rdlog(LOG_INFO, "Waiting kafka handler to stop properly");

	/* Make sure all outstanding requests are transmitted and handled. */
	while (rd_kafka_outq_len(global_config.rk) > 0) {
		rd_kafka_poll(global_config.rk, 50);
	}

	rd_kafka_destroy(global_config.rk);
	while (0 != rd_kafka_wait_destroyed(5000))
		;

	free(stats.append_buf.buf);
}
