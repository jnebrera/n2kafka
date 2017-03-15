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

#include "kafka_message_list.h"

#include <stdlib.h>
#include <string.h>

struct rd_kafka_message_queue_elm_s {
	/** Kafka message */
	rd_kafka_message_t msg;
	/** List entry */
	TAILQ_ENTRY(rd_kafka_message_queue_elm_s) list_entry;
};

void rd_kafka_msg_q_init(rd_kafka_message_queue_t *q) {
	q->count = 0;
	TAILQ_INIT(&q->list);
}

int rd_kafka_msg_q_add(rd_kafka_message_queue_t *q,
		       const rd_kafka_message_t *msg) {
	rd_kafka_message_queue_elm_t *elm = malloc(sizeof(*elm));
	if (elm) {
		++q->count;
		memcpy(&elm->msg, msg, sizeof(msg[0]));
		TAILQ_INSERT_TAIL(&q->list, elm, list_entry);
	}

	return elm != NULL;
}

static void
rd_kafka_msg_q_dump0(rd_kafka_message_queue_t *q, rd_kafka_message_t *msgs) {

	rd_kafka_message_queue_elm_t *elm = NULL;
	size_t i = 0;

	while ((elm = TAILQ_FIRST(&q->list))) {
		TAILQ_REMOVE(&q->list, elm, list_entry);
		if (msgs) {
			memcpy(&msgs[i++], &elm->msg, sizeof(msgs[0]));
		}
		free(elm);
	}

	q->count = 0;
}

void rd_kafka_msg_q_dump(rd_kafka_message_queue_t *q,
			 rd_kafka_message_t *msgs) {

	rd_kafka_msg_q_dump0(q, msgs);
}

void rd_kafka_msg_q_clean(rd_kafka_message_queue_t *q) {
	rd_kafka_msg_q_dump0(q, NULL);
}
