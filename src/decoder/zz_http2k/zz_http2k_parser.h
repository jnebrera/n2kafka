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

#include "zz_database.h"

#include <jansson.h>
#include <util/kafka_message_list.h>
#include <util/pair.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_parse.h>

#include <assert.h>

enum warning_times_pos {
	LAST_WARNING_TIME__QUEUE_FULL,
	LAST_WARNING_TIME__MSG_SIZE_TOO_LARGE,
	LAST_WARNING_TIME__UNKNOWN_PARTITION,
	LAST_WARNING_TIME__UNKNOWN_TOPIC,
	LAST_WARNING_TIME__END
};

/// @TODO many of the fields here could be a state machine
/// @TODO separate parsing <-> not parsing fields
/// @TODO could this be private?
struct zz_session {
#ifndef NDEBUG
#define ZZ_SESSION_MAGIC 0x535510A1C535510A
	uint64_t magic;
#endif
	/// Output generator.
	yajl_gen gen;

	/// JSON handler
	yajl_handle handler;

	/// Topid handler
	struct topic_s *topic_handler;

	/// Parsing stack position
	size_t stack_pos;

	/// Message list in this call to decode()
	rd_kafka_message_queue_t msg_queue;

	/// Error last warning
	time_t produce_error_last_time[LAST_WARNING_TIME__END];
};

static void __attribute__((unused))
assert_zz_session(const struct zz_session *sess) {
#ifdef ZZ_SESSION_MAGIC
	assert(ZZ_SESSION_MAGIC == sess->magic);
#else
	(void)sess;
#endif
}

int new_zz_session(struct zz_session *sess,
		   struct zz_database *zz_db,
		   const keyval_list_t *msg_vars);

int gen_jansson_object(yajl_gen gen, json_t *enrichment_data);

void free_zz_session(struct zz_session *sess);
