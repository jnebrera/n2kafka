/*
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
** Author: Eugenio Perez <eupm90@gmail.com>
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

#include "util.h"
/// @TODO this should not have engine/ dependences
#include "engine/global_config.h"
#include "engine/parse.h"
#include "util/kafka_message_array.h"

#include <pthread.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ERROR_BUFFER_SIZE 256
#define RDKAFKA_ERRSTR_SIZE ERROR_BUFFER_SIZE

/** Creates a new topic handler using global configuration
    @param topic_name Topic name
    @param partitioner Partitioner function
    @return New topic handler */
rd_kafka_topic_t *new_rkt_global_config(const char *topic_name,
					rb_rd_kafka_partitioner_t partitioner) {
	rd_kafka_topic_conf_t *template_config = global_config.kafka_topic_conf;
	rd_kafka_topic_conf_t *my_rkt_conf =
			rd_kafka_topic_conf_dup(template_config);

	if (NULL == my_rkt_conf) {
		rdlog(LOG_ERR,
		      "Couldn't topic_conf_dup in topic %s",
		      topic_name);
		return NULL;
	}

	rd_kafka_topic_conf_set_partitioner_cb(my_rkt_conf, partitioner);

	rd_kafka_topic_t *ret = rd_kafka_topic_new(
			global_config.rk, topic_name, my_rkt_conf);
	if (NULL == ret) {
		rdlog(LOG_ERR,
		      "Couldn't create kafka topic: %s",
		      gnu_strerror_r(errno));
		rd_kafka_topic_conf_destroy(my_rkt_conf);
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
		      "Message delivered (%zd bytes): %*.*s",
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

int32_t rb_client_mac_partitioner(const rd_kafka_topic_t *_rkt,
				  const void *key __attribute__((unused)),
				  size_t keylen __attribute__((unused)),
				  int32_t partition_cnt,
				  void *rkt_opaque,
				  void *msg_opaque) {
	const uint64_t client_mac = (uint64_t)(intptr_t)msg_opaque;
	if (client_mac == 0)
		return rd_kafka_msg_partitioner_random(_rkt,
						       NULL,
						       0,
						       partition_cnt,
						       rkt_opaque,
						       msg_opaque);
	else
		return client_mac % (unsigned)partition_cnt;
}

void init_rdkafka() {
	char errstr[RDKAFKA_ERRSTR_SIZE];

	assert(global_config.kafka_conf);

	if (only_stdout_output()) {
		rblog(LOG_DEBUG,
		      "No brokers and no topic specified. Output "
		      "will be printed in stdout.");
		return;
	}

	rd_kafka_conf_t *my_kafka_conf =
			rd_kafka_conf_dup(global_config.kafka_conf);
	rd_kafka_conf_set_dr_msg_cb(my_kafka_conf, msg_delivered);
	if (NULL == my_kafka_conf) {
		fatal("%% Failed to duplicate kafka conf (out of memory?)");
	}
	global_config.rk = rd_kafka_new(RD_KAFKA_PRODUCER,
					my_kafka_conf,
					errstr,
					RDKAFKA_ERRSTR_SIZE);

	if (!global_config.rk) {
		fatal("%% Failed to create new producer: %s", errstr);
	}

	if (global_config.debug) {
		rd_kafka_set_log_level(global_config.rk, LOG_DEBUG);
	}

	if (global_config.brokers == NULL) {
		fatal("%% No brokers specified");
	}
}

static void flush_kafka0(int timeout_ms) {
	rd_kafka_poll(global_config.rk, timeout_ms);
}

void send_to_kafka(rd_kafka_topic_t *rkt,
		   char *buf,
		   const size_t bufsize,
		   int flags,
		   void *opaque) {
	int retried = 0;

	do {
		if (NULL == rkt) {
			rdlog(LOG_ERR,
			      "Can't produce message, no topic specified");
			if (flags & RD_KAFKA_MSG_F_FREE) {
				free(buf);
			}
		}

		const int produce_ret = rd_kafka_produce(rkt,
							 RD_KAFKA_PARTITION_UA,
							 flags,
							 buf,
							 bufsize,
							 NULL,
							 0,
							 opaque);

		if (produce_ret == 0)
			break;

		if (ENOBUFS == errno && !(retried++)) {
			rd_kafka_poll(global_config.rk, 5); // backpressure
		} else {
			// rdbg(LOG_ERR, "Failed to produce message:
			// %s",rd_kafka_errno2err(errno));
			rblog(LOG_ERR,
			      "Failed to produce message: %s",
			      gnu_strerror_r(errno));
			if (flags & RD_KAFKA_MSG_F_FREE)
				free(buf);
			break;
		}
	} while (1);
}

void flush_kafka() {
	flush_kafka0(1000);
}

void kafka_poll(int timeout_ms) {
	rd_kafka_poll(global_config.rk, timeout_ms);
}

void stop_rdkafka() {
	rdlog(LOG_INFO, "Waiting kafka handler to stop properly");

	/* Make sure all outstanding requests are transmitted and handled. */
	while (rd_kafka_outq_len(global_config.rk) > 0) {
		rd_kafka_poll(global_config.rk, 50);
	}

	rd_kafka_topic_conf_destroy(global_config.kafka_topic_conf);
	rd_kafka_conf_destroy(global_config.kafka_conf);

	rd_kafka_destroy(global_config.rk);
	while (0 != rd_kafka_wait_destroyed(5000))
		;
}
