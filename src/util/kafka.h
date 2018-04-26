/*
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
** Copyright (C) 2018, Wizzie S.L.
** Author: Eugenio Perez <eupm90@gmail.com>
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
#include "engine/parse.h"
#include "util/pair.h"
#include <librdkafka/rdkafka.h>

#include <string.h>

/* Private data */
struct rd_kafka_message_s;

void init_rdkafka();

void kafka_poll();

typedef int32_t (*rb_rd_kafka_partitioner_t)(const rd_kafka_topic_t *rkt,
					     const void *keydata,
					     size_t keylen,
					     int32_t partition_cnt,
					     void *rkt_opaque,
					     void *msg_opaque);

/** Creates a new topic handler using global configuration
    @param topic_name Topic name
    @param partitioner Partitioner function
    @return New topic handler */
rd_kafka_topic_t *new_rkt_global_config(const char *topic_name,
					rb_rd_kafka_partitioner_t partitioner);

/** Default kafka topic name (if any)
	@return Default kafka topic name (if any)
	*/
const char *default_topic_name();

void flush_kafka();
void stop_rdkafka();
