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

void random_topic_name(char *template) {
	/// n2kafka temp topic
	int fd = mkstemp(template);
	if (fd < 0) {
		char errmsg_buf[512];
		fail_msg("Failed to create temp file %s: %s",
			 template,
			 strerror_r(errno, errmsg_buf, sizeof(errmsg_buf)));
	}

	close(fd);
}

rd_kafka_t *init_kafka_consumer(const char *brokers, const char *topic) {
	rd_kafka_t *rk;

	// Kafka
	char errstr[512];
	rd_kafka_topic_partition_list_t *topics;
	rd_kafka_resp_err_t err;
	rd_kafka_conf_t *conf = rd_kafka_conf_new();
	rd_kafka_topic_conf_t *topic_conf = rd_kafka_topic_conf_new();

	// Set group id
	if (rd_kafka_conf_set(conf,
			      "group.id",
			      "tester",
			      errstr,
			      sizeof(errstr)) != RD_KAFKA_CONF_OK) {
		fprintf(stderr, "%% %s\n", errstr);
		exit(1);
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

	size_t i;
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
		fprintf(stderr,
			"%% Failed to create new consumer: %s\n",
			errstr);
		exit(1);
	}

	// Add brokers
	if (rd_kafka_brokers_add(rk, brokers) == 0) {
		fprintf(stderr, "%% No valid brokers specified\n");
		exit(1);
	}

	// Redirect rd_kafka_poll() to consumer_poll()
	rd_kafka_poll_set_consumer(rk);

	// Topic list
	topics = rd_kafka_topic_partition_list_new(1);
	rd_kafka_topic_partition_list_add(topics, topic, 0);

	// Assign partitions
	if ((err = rd_kafka_assign(rk, topics))) {
		fprintf(stderr,
			"%% Failed to assign partitions: %s\n",
			rd_kafka_err2str(err));
	}

	rd_kafka_topic_partition_list_destroy(topics);

	return rk;
}
