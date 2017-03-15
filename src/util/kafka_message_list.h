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
#include <sys/queue.h>

struct rd_kafka_message_queue_elm_s;

/** Queue element with one rdkafka message */
typedef struct rd_kafka_message_queue_elm_s rd_kafka_message_queue_elm_t;

/** Kafka message queue */
typedef struct rd_kafka_message_queue_s {
	/** Number of elements */
	size_t count;
	/** Actual list */
	TAILQ_HEAD(, rd_kafka_message_queue_elm_s) list;
} rd_kafka_message_queue_t;

#define rd_kafka_msg_q_size(q) ((q)->count)

/** Init a message queue
	@param q Queue */
void rd_kafka_msg_q_init(rd_kafka_message_queue_t *q);

/** Add a message to the message queue
	@param q Queue
	@param msg Message
	@note The message will be copied
	@return 1 if ok, 0 if no memory avalable */
int rd_kafka_msg_q_add(rd_kafka_message_queue_t *q,
		       const rd_kafka_message_t *msg);

/** Dump messages to an allocated messages array. It is
	suppose to be able to hold as many messages as
	rd_kafka_msg_q_size(q)
	@param q Queue
	@param msgs Messages to dump to
	@note after this call, queue will be empty */
void rd_kafka_msg_q_dump(rd_kafka_message_queue_t *q, rd_kafka_message_t *msgs);

/** Discards all messages in queue
	@param q Queue
	*/
void rd_kafka_msg_q_clean(rd_kafka_message_queue_t *q);
