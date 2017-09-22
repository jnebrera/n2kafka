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

#include "n2k_kafka_tests.h"

#include <setjmp.h>

#include <cmocka.h>

#include <librd/rd.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int valid_topic_name(const char *topic_name) {
	static const char valid_chars[] =
			"abcdefghijklmnopqrstuvwxyz"
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-";
	return strspn(topic_name, valid_chars) == strlen(topic_name);
}

void random_topic_name(char *template) {
	/// n2kafka temp topic
	int valid_topic = 0;
	const size_t template_len = strlen(template);
	for (valid_topic = 0; !valid_topic;
	     valid_topic = valid_topic_name(template)) {
		int fd = mkstemp(template);
		if (fd < 0) {
			assert_return_code(fd, errno);
		}

		valid_topic = valid_topic_name(template);
		if (!valid_topic) {
			static const char template_pattern[] = "XXXXXX";
			memcpy(&template[template_len -
					 strlen(template_pattern)],
			       template_pattern,
			       strlen(template_pattern));
		}
		close(fd);
	}
}

rd_kafka_t *init_kafka_consumer(const char *brokers) {
	rd_kafka_t *rk;

	// Kafka
	char errstr[512];
	rd_kafka_conf_t *conf = rd_kafka_conf_new();
	rd_kafka_topic_conf_t *topic_conf = rd_kafka_topic_conf_new();

	struct {
		const char *key, *value;
	} rk_props[] = {
			{
					// No need for different groups ids
					.key = "group.id",
					.value = "tester",
			},
			{
					.key = "metadata.broker.list",
					.value = brokers,
			},
			{
					.key = "fetch.wait.max.ms",
					.value = "5",
			},
			{
					.key = "fetch.error.backoff.ms",
					.value = "10",
			},
	};

	size_t i;
	for (i = 0; i < RD_ARRAYSIZE(rk_props); ++i) {
		const rd_kafka_conf_res_t set_prop_rc =
				rd_kafka_conf_set(conf,
						  rk_props[i].key,
						  rk_props[i].value,
						  errstr,
						  sizeof(errstr));
		if (set_prop_rc != RD_KAFKA_CONF_OK) {
			fail_msg("Couldn't set rk consumer %s property to [%s]:"
				 " %s",
				 rk_props[i].key,
				 rk_props[i].value,
				 errstr);
		}
	}

	// Version fallback. Needed for newer brokers
	// if (rd_kafka_conf_set(conf, "broker.version", "0.8.2", errstr,
	//                       sizeof(errstr)) != RD_KAFKA_CONF_OK) {
	//   fprintf(stderr, "%% %s\n", errstr);
	//   exit(1);
	// }

	struct topic_props {
		const char *key, *value;
	} topic_confs[] = {
			// clang-format off
		{
			.key = "offset.store.method",
			.value = "broker",
		},
		{
			.key = "auto.offset.reset",
			.value = "earliest",
		}
			// clang-format on
	};

	for (i = 0; i < RD_ARRAYSIZE(topic_confs); ++i) {
		const rd_kafka_conf_res_t rc =
				rd_kafka_topic_conf_set(topic_conf,
							topic_confs[i].key,
							topic_confs[i].value,
							errstr,
							sizeof(errstr));
		if (rc != RD_KAFKA_CONF_OK) {
			fail_msg("Couldn't set topic %s=%s prop: %s",
				 topic_confs[i].key,
				 topic_confs[i].value,
				 errstr);
		}
	}

	rd_kafka_conf_set_default_topic_conf(conf, topic_conf);

	// Create Kafka handle
	if (!(rk = rd_kafka_new(RD_KAFKA_CONSUMER,
				conf,
				errstr,
				sizeof(errstr)))) {
		fail_msg("Failed to create new consumer: %s\n", errstr);
	}

	// Redirect rd_kafka_poll() to consumer_poll()
	rd_kafka_poll_set_consumer(rk);

	return rk;
}

void set_rdkafka_consumer_topics(rd_kafka_t *rk, const char *topic) {
	// Topic list
	rd_kafka_topic_partition_list_t *topics =
			rd_kafka_topic_partition_list_new(1);
	rd_kafka_topic_partition_list_add(topics, topic, 0);

	// Assign partitions
	rd_kafka_resp_err_t err = rd_kafka_assign(rk, topics);
	if (RD_KAFKA_RESP_ERR_NO_ERROR != err) {
		fail_msg("Failed to assign partitions: %s\n",
			 rd_kafka_err2str(err));
	}

	rd_kafka_topic_partition_list_destroy(topics);
}

static bool
bool_array_all(const bool *array, size_t array_len, bool test_value) {
	for (size_t i = 0; i < array_len; ++i) {
		if (array[i] != test_value) {
			return false;
		}
	}

	return true;
}

void consume_kafka_messages(rd_kafka_t *rk,
			    const char *expected_topic,
			    const char **expected_kafka_messages,
			    rd_kafka_topic_partition_list_t *topic_list,
			    bool *eof_reached) {

	if (NULL == *expected_kafka_messages) {
		// We are only expecting for EOF
		if (bool_array_all(eof_reached,
				   (size_t)topic_list->cnt,
				   true)) {
			return;
		}
	}

	while (true) {
		rd_kafka_message_t *rkmessage =
				rd_kafka_consumer_poll(rk, 60000);
		if (NULL == rkmessage) {
			fail_msg("Timeout consuming with kafka");
		}

		const char *topic_name = rd_kafka_topic_name(rkmessage->rkt);

		if (RD_KAFKA_RESP_ERR__PARTITION_EOF == rkmessage->err) {
			rd_kafka_topic_partition_t *eof =
					rd_kafka_topic_partition_list_find(
							topic_list,
							topic_name,
							rkmessage->partition);
			assert_non_null(eof);
			eof_reached[eof - topic_list->elems] = true;

			if (bool_array_all(eof_reached,
					   (size_t)topic_list->cnt,
					   true)) {
				return;
			}

		} else if (0 != rkmessage->err) {
			fail_msg("Error consuming from topic %s: %s",
				 topic_name,
				 rd_kafka_message_errstr(rkmessage));
		} else if ((expected_topic &&
			    strcmp(topic_name, expected_topic)) ||
			   NULL == *expected_kafka_messages) {
			expected_topic = expected_topic ?: "(null)";
			fail_msg("Unexpected message received from topic %s, "
				 "expected %s",
				 topic_name,
				 expected_topic);
		} else {
			assert_int_equal(
					0,
					strncmp(rkmessage->payload,
						*expected_kafka_messages,
						strlen(*expected_kafka_messages)));
			expected_kafka_messages++;
		}

		rd_kafka_message_destroy(rkmessage);
	}
}

void reach_eof(rd_kafka_t *rk,
	       rd_kafka_topic_partition_list_t *topic_list,
	       bool *eof_reached) {
	const char *expected_topic = NULL;
	const char **expected_kafka_messages = (const char *[]){NULL};

	consume_kafka_messages(rk,
			       expected_topic,
			       expected_kafka_messages,
			       topic_list,
			       eof_reached);
}
