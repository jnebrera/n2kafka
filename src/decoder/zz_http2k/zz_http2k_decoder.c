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

#include "zz_http2k_decoder.h"

#include "zz_database.h"
#include "zz_http2k_parser.h"

#include "util/kafka_message_array.h"
#include "util/pair.h"
#include "util/string.h"
#include "util/topic_database.h"
#include "util/util.h"

#include <jansson.h>
#include <librd/rdlog.h>
#include <librdkafka/rdkafka.h>
#include <yajl/yajl_parse.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

static struct zz_database zz_database = {NULL};

static int zz_decoder_init(const struct json_t *config) {
	(void)config;

	const int init_db_rc = init_zz_database(&zz_database);
	if (init_db_rc != 0) {
		rdlog(LOG_ERR, "Couldn't init zz_database");
		return -1;
	}

	return 0;
}

/** Main decoder entrypoint, borrowing buffer
    @param received HTTP chunk buffer
    @param buf_size JSON chunk buffer size
    @param props HTTP properties
    @param response Static allocated response
    @param response_size Response size
    @param t_decoder_opaque Decoder opaque - unused
    @param t_session Decoder session
    @return Proper decoder error
    */
static enum decoder_callback_err zz_decode(const char *buffer,
					   size_t buf_size,
					   const keyval_list_t *props,
					   void *t_decoder_opaque,
					   const char **response,
					   size_t *response_size,
					   void *t_session) {
	(void)t_decoder_opaque;

	const char *http_method = valueof(props, "D-HTTP-method", strcmp);
	if (unlikely(http_method && 0 != strcmp("POST", http_method))) {
		return DECODER_CALLBACK_HTTP_METHOD_NOT_ALLOWED;
	}

	assert(buffer);

	struct zz_session *session = zz_session_cast(t_session);
	assert(session->topic_handler);

	kafka_msg_array_init(&session->kafka_msgs);
	const enum decoder_callback_err process_err =
			session->process_buffer(buffer, buf_size, session);

	//
	// Clean & consume all info generated in this call
	//
	const size_t kafka_messages_count =
			kafka_message_array_size(&session->kafka_msgs);
	size_t kafka_messages_sent = 0;
	if (kafka_messages_count) {
		rd_kafka_topic_t *rkt = topics_db_get_rdkafka_topic(
				session->topic_handler);

		kafka_messages_sent = kafka_message_array_produce(
				rkt,
				&session->kafka_msgs,
				0 /* rdkafka flags */,
				&session->kafka_msgs_last_warning);
	}

	memset(&session->kafka_msgs, 0, sizeof(session->kafka_msgs));

	//
	// Return information
	//
	if (unlikely(process_err != DECODER_CALLBACK_OK ||
		     kafka_messages_count != kafka_messages_sent)) {

		// clang-format off
		const enum decoder_callback_err rc =
			(process_err != DECODER_CALLBACK_OK) ?
				process_err
			/*(kafka_messages_count != kafka_messages_sent) ? */ :
				DECODER_CALLBACK_BUFFER_FULL;
		// clang-format on

		string full_error =
				session->error_message(session->http_response,
						       process_err,
						       kafka_messages_sent);

		if (string_size(&full_error) > 0) {
			string_done(&session->http_response);
			session->http_response = full_error;
		}

		*response = session->http_response.buf;
		*response_size = session->http_response.size;

		return rc;
	}

	return DECODER_CALLBACK_OK;
}

static void zz_decoder_done() {
	free_valid_zz_database(&zz_database);
}

static const char *zz_name() {
	return "zz_http2k";
}

static int vnew_zz_session(void *t_session,
			   void *listener_opaque,
			   const keyval_list_t *msg_vars) {
	(void)listener_opaque;
	struct zz_session *session = t_session;
	return new_zz_session(session, &zz_database, msg_vars);
}

static void vfree_zz_session(void *t_session) {
	free_zz_session(zz_session_cast(t_session));
}

static size_t size_align_to(size_t size, size_t alignment) {
	return size % alignment == 0 ? size
				     : (size / alignment + 1) * alignment;
}

static size_t zz_session_size() {
	// Alignment to 128bits
	return size_align_to(sizeof(struct zz_session), 16);
}

const struct n2k_decoder zz_decoder = {
		.name = zz_name,

		.init = zz_decoder_init,
		.done = zz_decoder_done,

		.new_session = vnew_zz_session,
		.delete_session = vfree_zz_session,
		.session_size = zz_session_size,

		.callback = zz_decode,
};
