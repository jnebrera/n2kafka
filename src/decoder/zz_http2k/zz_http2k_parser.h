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

#include <util/kafka_message_array.h>
#include <util/pair.h>
#include <yajl/yajl_parse.h>

#include <assert.h>

/// @TODO many of the fields here could be a state machine
/// @TODO separate parsing <-> not parsing fields
/// @TODO could this be private?
struct zz_session {
#ifndef NDEBUG
#define ZZ_SESSION_MAGIC 0x535510A1C535510A
	uint64_t magic;
#endif

	/// JSON handler
	yajl_handle handler;

	/// Topid handler
	struct topic_s *topic_handler;

	/// Parsing stack position
	size_t stack_pos;

	/// Warning state of kafka_msgs_produce
	struct kafka_message_array_last_warning_state kafka_msgs_last_warning;

	/// Previous chunk object, in case that
	struct {
		string last_object;
	} http_prev_chunk;

	/// Messages sent in this session
	size_t session_messages_sent;

	/// HTTP response
	string http_response;

	/// Per chunk information
	struct {
		kafka_message_array kafka_msgs; ///< Kafka output messages
		const char *in_buffer;     ///< current yajl_parse call chunk
		const char *last_open_map; ///< Last seen open map
	} http_chunk;
};

/// Cast an opaque pointer to zz_session
static struct zz_session *zz_session_cast(void *opaque) RD_UNUSED;
static struct zz_session *zz_session_cast(void *opaque) {
	struct zz_session *ret = opaque;
#ifdef ZZ_SESSION_MAGIC
	assert(ZZ_SESSION_MAGIC == ret->magic);
#endif
	return ret;
}

int new_zz_session(struct zz_session *sess,
		   struct zz_database *zz_db,
		   const keyval_list_t *msg_vars);

void free_zz_session(struct zz_session *sess);
