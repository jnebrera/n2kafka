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

#pragma once

#include <librdkafka/rdkafka.h>
#include <stdbool.h>

/// Return unique random topic name
void random_topic_name(char *template);

rd_kafka_t *init_kafka_consumer(const char *brokers);
void set_rdkafka_consumer_topics(rd_kafka_t *rk, const char *topic);

/// Consume all kafka messages in specified topic
void consume_kafka_messages(rd_kafka_t *rk,
			    const char *expected_topic,
			    const char **expected_kafka_messages,
			    rd_kafka_topic_partition_list_t *topic_list,
			    bool *eof_reached);

/// Reach EOF in all topics
void reach_eof(rd_kafka_t *rk,
	       rd_kafka_topic_partition_list_t *topic_list,
	       bool *eof_reached);
