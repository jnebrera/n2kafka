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

#pragma once

#include <librdkafka/rdkafka.h>

#include <stdint.h>
#include <string.h>

/* Private data */
struct rd_kafka_message_s;

/// rdkafka options
typedef struct n2kafka_rdkafka_conf {
	/// Options that can be mapped directly to a rdkafka conf
	rd_kafka_conf_t *rk_conf;

	/// Stats related options
	struct {
		/// Stats destination topic
		const char *topic;
	} statistics;
} n2kafka_rdkafka_conf;

/**
 * @brief      Init rdkafka system
 *
 * @param      kafka_conf   The kafka configuration
 * @param[in]  stats_topic  The topic to send rdkafka statistics. If NULL and
 *                          rdkafka.statistics.interval.ms > 0, they are print
 *                          to stdout
 */
void init_rdkafka(n2kafka_rdkafka_conf *conf);

void kafka_poll();

/** Creates a new topic handler using global configuration
    @param topic_name Topic name
    @param partitioner Partitioner function
    @return New topic handler */
rd_kafka_topic_t *new_rkt_global_config(const char *topic_name);

/** Default kafka topic name (if any)
	@return Default kafka topic name (if any)
	*/
const char *default_topic_name();

/**
 * @brief      Set the n2kafka callbacks to rdkafka handler configuration.
 *
 * @param      conf  The kafka conf
 */
void rdkafka_conf_set_n2kafka_callbacks(rd_kafka_conf_t *conf);

void flush_kafka();
void stop_rdkafka();
