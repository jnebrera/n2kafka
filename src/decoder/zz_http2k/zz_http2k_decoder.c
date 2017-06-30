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

#include "zz_http2k_decoder.h"
#include "zz_database.h"
#include "zz_http2k_parser.h"

#include "engine/global_config.h"

#include "util/kafka.h"
#include "util/kafka_message_array.h"
#include "util/rb_time.h"
#include "util/topic_database.h"
#include "util/util.h"

#include <assert.h>
#include <errno.h>
#include <jansson.h>
#include <librd/rd.h>
#include <librd/rdlog.h>
#include <librd/rdmem.h>
#include <librdkafka/rdkafka.h>
#include <stdint.h>
#include <string.h>

static struct zz_database zz_database = {NULL};

static int zz_decoder_init(const struct json_t *config) {
	(void)config;
	if (only_stdout_output()) {
		rdlog(LOG_ERR,
		      "Can't use zz_http2k decoder if not kafka "
		      "brokers configured.");
		return -1;
	}

	const int init_db_rc = init_zz_database(&zz_database);
	if (init_db_rc != 0) {
		rdlog(LOG_ERR, "Couldn't init zz_database");
		return -1;
	}

	return 0;
}

/*
 *  MAIN ENTRY POINT
 */

static void process_zz_buffer(const char *buffer,
			      size_t bsize,
			      struct zz_session *session) {

	assert(session);
	const unsigned char *in_iterator = (const unsigned char *)buffer;

	yajl_status stat = yajl_parse(session->handler, in_iterator, bsize);

	if (stat != yajl_status_ok) {
		/// @TODO improve this!
		unsigned char *str = yajl_get_error(
				session->handler, 1, in_iterator, bsize);
		fprintf(stderr, "%s", (const char *)str);
		yajl_free_error(session->handler, str);
	}
}

static void zz_decode0(char *buffer,
		       size_t buf_size,
		       const keyval_list_t *props,
		       void *t_decoder_opaque,
		       void *t_session) {
	(void)props;
	(void)t_decoder_opaque;
	assert(buffer);

	struct zz_session *session = zz_session_cast(t_session);
	assert(session->topic_handler);

	kafka_msg_array_init(&session->http_chunk.kafka_msgs);
	session->http_chunk.in_buffer = buffer;
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

	if (kafka_message_array_size(&session->http_chunk.kafka_msgs)) {
		rd_kafka_topic_t *rkt = topics_db_get_rdkafka_topic(
				session->topic_handler);

		kafka_message_array_produce(rkt,
					    &session->http_chunk.kafka_msgs,
					    buffer,
					    0 /* rdkafka flags */,
					    &session->kafka_msgs_last_warning);
	} else {
		free(buffer);
	}

	memset(&session->http_chunk, 0, sizeof(session->http_chunk));
}

/// zz_decode with const buffer, need to copy it
static void zz_decode(const char *buffer,
		      size_t buf_size,
		      const keyval_list_t *props,
		      void *t_decoder_opaque,
		      void *t_session) {

	char *buffer_copy = malloc(buf_size);
	if (unlikely(NULL == buffer_copy)) {
		rdlog(LOG_ERR, "Couldn't copy buffer (OOM?)");
		/// @todo return error!
	} else {
		memcpy(buffer_copy, buffer, buf_size);
		zz_decode0(buffer_copy,
			   buf_size,
			   props,
			   t_decoder_opaque,
			   t_session);
	}
}

static void zz_decoder_done() {
	free_valid_zz_database(&zz_database);
}

static const char *zz_name() {
	return "zz_http2k";
}

static const char *zz_config_token() {
	return "zz_http2k_config";
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
		.config_parameter = zz_config_token,

		.init = zz_decoder_init,
		.done = zz_decoder_done,

		.new_session = vnew_zz_session,
		.delete_session = vfree_zz_session,
		.session_size = zz_session_size,

		.callback = zz_decode,
};
