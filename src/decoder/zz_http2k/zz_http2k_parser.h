/*
**
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
** Copyright (C) 2018-2019, Wizzie S.L.
** Author: Eugenio Perez <eupm90@gmail.com>
**
** This file is part of n2kafka.
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

#include "config.h"

#include "zz_http2k_parser_json.h"

#if WITH_EXPAT
#include "zz_http2k_parser_xml.h"
#endif // WITH_EXPAT

#include "util/kafka_message_array.h"
#include "util/pair.h"
#include "util/string.h"

#include <librdkafka/rdkafka.h>

#include <stddef.h>

struct zz_database;

/// @TODO many of the fields here could be a state machine
/// @TODO separate parsing <-> not parsing fields
/// @TODO could this be private?
struct zz_session {
#ifndef NDEBUG
#define ZZ_SESSION_MAGIC 0x535510A1C535510A
	uint64_t magic;
#endif

	union {
		zz_json_session json_session;
#if WITH_EXPAT
		zz_xml_session xml_session;
#endif // WITH_EXPAT
	};

	/// Topid handler
	struct topic_s *topic_handler;

	/// Kafka output messages
	kafka_message_array kafka_msgs;

	/// Warning state of kafka_msgs_produce
	struct kafka_message_array_last_warning_state kafka_msgs_last_warning;

	/// Messages sent in this session
	size_t session_messages_sent;

	/// HTTP response
	string http_response;

	/// Process buffer callback
	enum decoder_callback_err (*process_buffer)(const char *buffer,
						    size_t bsize,
						    struct zz_session *session);

	/// Free session callback
	void (*free_session)(struct zz_session *session);

	/// Final error message callback
	string (*error_message)(string decoder_err,
				enum decoder_callback_err process_err,
				size_t kafka_messages_sent);
};

/// Cast an opaque pointer to zz_session
struct zz_session *zz_session_cast(void *opaque);

int new_zz_session(struct zz_session *sess,
		   struct zz_database *zz_db,
		   const keyval_list_t *msg_vars);

void free_zz_session(struct zz_session *sess);
