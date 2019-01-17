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

#include "engine/global_config.h"
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

/** Decode a JSON chunk
    @param buffer JSONs buffer
    @param bsize buffer size
    @param session ZZ messages session
    @return DECODER_CALLBACK_OK if all went OK, DECODER_CALLBACK_INVALID_REQUEST
	    if request was invalid. In latter case, session->http_response will
	be filled with JSON error
    */
static enum decoder_callback_err process_zz_buffer(const char *buffer,
						   size_t bsize,
						   struct zz_session *session) {

	assert(session);
	const unsigned char *in_iterator = (const unsigned char *)buffer;

	yajl_status stat = yajl_parse(session->handler, in_iterator, bsize);

	if (unlikely(stat != yajl_status_ok)) {
		static const int yajl_verbose = 1;
		char *str = (char *)yajl_get_error(session->handler,
						   yajl_verbose,
						   in_iterator,
						   bsize);
		rdlog(LOG_ERR, "Invalid entry JSON:\n%s", (const char *)str);
		string_append(&session->http_response, str, strlen(str));
		yajl_free_error(session->handler, (unsigned char *)str);
		return DECODER_CALLBACK_INVALID_REQUEST;
	}

	return DECODER_CALLBACK_OK;
}

/** Main decoder entrypoint, borrowing buffer
    @param buffer JSON chunk buffer
    @param buf_size JSON chunk buffer size
    @param props HTTP properties
    @param response Static allocated response
    @param response_size Response size
    @param t_decoder_opaque Decoder opaque - unused
    @param t_session Decoder session
    @return Proper decoder error
    */
static enum decoder_callback_err zz_decode0(char *buffer,
					    size_t buf_size,
					    const keyval_list_t *props,
					    void *t_decoder_opaque,
					    const char **response,
					    size_t *response_size,
					    void *t_session) {
	(void)t_decoder_opaque;

	const char *http_method = valueof(props, "D-HTTP-method");
	if (unlikely(http_method && 0 != strcmp("POST", http_method))) {
		return DECODER_CALLBACK_HTTP_METHOD_NOT_ALLOWED;
	}

	assert(buffer);

	struct zz_session *session = zz_session_cast(t_session);
	assert(session->topic_handler);

	kafka_msg_array_init(&session->http_chunk.kafka_msgs);
	session->http_chunk.in_buffer = buffer;
	const enum decoder_callback_err process_err =
			process_zz_buffer(buffer, buf_size, session);

	//
	// Check if we are in the middle of JSON object processing
	//

	// clang-format off
	const int append_rc =
		(session->http_chunk.last_open_map) ?
			// JSON object is not closed, save it for the next
			// decode call
			string_append(&session->http_prev_chunk.last_object,
			              session->http_chunk.last_open_map,
			              buf_size -
			                 (size_t)(
			                       session->http_chunk.last_open_map
			                       - buffer))
		: (string_size(&session->http_prev_chunk.last_object) != 0) ?
			// process_zz_buffer did not consume last object buffer,
			// so JSON object is divided in >2 chunks. Act like all
			// JSON object was in the last message!
			string_append(&session->http_prev_chunk.last_object,
			              buffer,
			              buf_size)
		: 0;
	// clang-format on

	if (unlikely(append_rc != 0)) {
		rdlog(LOG_ERR,
		      "Couldn't append chunked JSON object to temp buffer "
		      "(OOM?)");
		// @TODO signal error, buffer is not valid anymore!
	}

	//
	// Clean & consume all info generated in this call
	//
	const size_t kafka_messages_count = kafka_message_array_size(
			&session->http_chunk.kafka_msgs);
	size_t kafka_messages_sent = 0;
	if (kafka_messages_count) {
		rd_kafka_topic_t *rkt = topics_db_get_rdkafka_topic(
				session->topic_handler);

		kafka_messages_sent = kafka_message_array_produce(
				rkt,
				&session->http_chunk.kafka_msgs,
				buffer,
				0 /* rdkafka flags */,
				&session->kafka_msgs_last_warning);
	} else {
		free(buffer);
	}

	memset(&session->http_chunk, 0, sizeof(session->http_chunk));

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

		string yajl_err = session->http_response;
		session->http_response = N2K_STRING_INITIALIZER;

		const int printf_rc = string_printf(&session->http_response,
						    "{\"messages_queued\":%zu",
						    kafka_messages_sent);
		if (printf_rc > 0 && process_err != DECODER_CALLBACK_OK) {
			string_append_string(&session->http_response,
					     ",\"json_decoder_error\":\"");
			string_append_json_string(&session->http_response,
						  yajl_err.buf,
						  string_size(&yajl_err));
			string_append_string(&session->http_response, "\"");
		}

		if (printf_rc > 0) {
			string_append_string(&session->http_response, "}");
			*response = session->http_response.buf;
			*response_size = session->http_response.size;
		}

		string_done(&yajl_err);

		return rc;
	}

	return DECODER_CALLBACK_OK;
}

/// zz_decode with const buffer, need to copy it
static enum decoder_callback_err zz_decode(const char *buffer,
					   size_t buf_size,
					   const keyval_list_t *props,
					   void *t_decoder_opaque,
					   const char **response,
					   size_t *response_size,
					   void *t_session) {
	char *buffer_copy = NULL;
	if (likely(buf_size)) {
		buffer_copy = malloc(buf_size);

		if (unlikely(NULL == buffer_copy)) {
			rdlog(LOG_ERR, "Couldn't copy buffer (OOM?)");
			return DECODER_CALLBACK_MEMORY_ERROR;
		}

		memcpy(buffer_copy, buffer, buf_size);
	}

	return zz_decode0(buffer_copy,
			  buf_size,
			  props,
			  t_decoder_opaque,
			  response,
			  response_size,
			  t_session);
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
