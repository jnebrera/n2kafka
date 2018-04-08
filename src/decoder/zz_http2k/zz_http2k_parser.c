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

#include "zz_http2k_parser.h"

#include "zz_database.h"

#include <jansson.h>
#include <librd/rdlog.h>
#include <librd/rdmem.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_parse.h>

#include <assert.h>
#include <string.h>

#define YAJL_PARSER_OK 1
#define YAJL_PARSER_ABORT 0

//
// PARSING
//

static int zz_parse_start_map(void *ctx) {
	struct zz_session *sess = ctx;
	if (0 == sess->stack_pos++) {
		sess->http_chunk.last_open_map =
				sess->http_chunk.in_buffer +
				yajl_get_bytes_consumed(sess->handler) -
				sizeof((char)'{');
	}
	return YAJL_PARSER_OK;
}

/** Generate kafka message.
    */
static void zz_parse_generate_rdkafka_message(struct zz_session *sess,
					      rd_kafka_message_t *msg) {
	assert(sess);
	assert(msg);
	const char *end_msg = sess->http_chunk.in_buffer +
			      yajl_get_bytes_consumed(sess->handler);
	assert(end_msg > sess->http_chunk.last_open_map);
	*msg = (rd_kafka_message_t){
			.payload = const_cast(sess->http_chunk.last_open_map),
			.len = (size_t)(end_msg -
					sess->http_chunk.last_open_map),
	};
}

static int zz_parse_end_map0(struct zz_session *sess) {
	rd_kafka_message_t msg;
	zz_parse_generate_rdkafka_message(sess, &msg);
	const int add_rc =
			kafka_msg_array_add(&sess->http_chunk.kafka_msgs, &msg);

	// This is not the last message anymore
	sess->http_chunk.last_open_map = NULL;
	if (unlikely(add_rc != 0)) {
		rdlog(LOG_ERR, "Couldn't add kafka message (OOM?)");
	}

	return YAJL_PARSER_OK;
}

/** Send a message of a split message
  @param sess Parsing message session
  @return do keep or not to keep parsing
  */
static int zz_parse_end_map_split(struct zz_session *sess) {
	const int append_rc =
			string_append(&sess->http_prev_chunk.last_object,
				      sess->http_chunk.in_buffer,
				      yajl_get_bytes_consumed(sess->handler));
	if (unlikely(append_rc != 0)) {
		rdlog(LOG_ERR, "Couldn't append message (OOM?)");
		goto err;
	}

	rd_kafka_topic_t *rkt =
			topics_db_get_rdkafka_topic(sess->topic_handler);
	const int produce_rc = rd_kafka_produce_batch(
			// clang-format off
		rkt,
		RD_KAFKA_PARTITION_UA,
		RD_KAFKA_MSG_F_FREE,
		&(rd_kafka_message_t){
			.payload = sess->http_prev_chunk.last_object.buf,
			.len = string_size(&sess->http_prev_chunk.last_object),
		},
		1);
	// clang-format on

	if (likely(1 == produce_rc)) {
		// librdkafka will free it
		sess->http_prev_chunk.last_object.buf = NULL;
	}

err:
	string_done(&sess->http_prev_chunk.last_object);

	return YAJL_PARSER_OK;
}

static int zz_parse_end_map(void *ctx) {
	struct zz_session *sess = ctx;

	if (0 != --sess->stack_pos) {
		return YAJL_PARSER_OK;
	}

	return (string_size(&sess->http_prev_chunk.last_object) > 0)
			       ?
			       // The message is divided between two chunks
			       zz_parse_end_map_split(sess)
			       :

			       zz_parse_end_map0(sess);
}

/* Return code: Valid uri prefix (i.e., /v1/data/) & topic */
static int
extract_url_topic(const char *url, const char **topic, size_t *topic_size) {
	assert(url);
	assert(topic);
	assert(topic_size);
	static const char url_specials[] = ";/?:@=&";
	static const char valid_prefix_str[] = "/v1/data/";
	const int valid_prefix = 0 == strncmp(valid_prefix_str,
					      url,
					      strlen(valid_prefix_str));

	if (unlikely(!valid_prefix)) {
		return -1;
	}

	*topic = url + strlen(valid_prefix_str);
	*topic_size = strcspn(*topic, url_specials);
	if (unlikely(*topic_size == 0)) {
		return -1;
	}

	return 0;
}

int new_zz_session(struct zz_session *sess,
		   struct zz_database *zz_db,
		   const keyval_list_t *msg_vars) {
	assert(sess);
	assert(zz_db);
	assert(msg_vars);
	const char *client_ip = valueof(msg_vars, "D-Client-IP");
	const char *url = valueof(msg_vars, "D-HTTP-URI");
	struct {
		const char *buf;
		size_t buf_len;
	} topic = {}, client_uuid = {};

	const int parse_url_rc =
			extract_url_topic(url, &topic.buf, &topic.buf_len);
	if (unlikely(0 != parse_url_rc)) {
		rdlog(LOG_ERR, "Couldn't extract url topic from %s", url);
		return -1;
	}
	client_uuid.buf = valueof(msg_vars, "X-Consumer-ID");

	if (client_uuid.buf) {
		client_uuid.buf_len = strlen(client_uuid.buf);
	}

	char uuid_topic[topic.buf_len + sizeof((char)'_') +
			client_uuid.buf_len + sizeof((char)'\0')];

	char *uuid_topic_cursor = uuid_topic;

	if (client_uuid.buf) {
		memcpy(uuid_topic, client_uuid.buf, client_uuid.buf_len);
		uuid_topic[client_uuid.buf_len] = '_';
		uuid_topic_cursor += client_uuid.buf_len + sizeof((char)'_');
	}

	memcpy(uuid_topic_cursor, topic.buf, topic.buf_len);
	uuid_topic_cursor[topic.buf_len] = '\0';

	memset(sess, 0, sizeof(*sess));
#ifdef ZZ_SESSION_MAGIC
	sess->magic = ZZ_SESSION_MAGIC;
#endif
	sess->topic_handler = zz_http2k_database_get_topic(
			zz_db, uuid_topic, time(NULL));
	if (unlikely(NULL == sess->topic_handler)) {
		rdlog(LOG_ERR,
		      "Invalid topic %s received from client %s",
		      topic.buf,
		      client_ip);
		goto topic_err;
	}

	static const yajl_callbacks yajl_callbacks = {
			.yajl_start_map = zz_parse_start_map,
			.yajl_end_map = zz_parse_end_map,
	};

	sess->handler = yajl_alloc(&yajl_callbacks, NULL, sess);
	if (NULL == sess->handler) {
		rdlog(LOG_CRIT, "Couldn't allocate yajl_handler");
		goto err_yajl_handler;
	}

	yajl_config(sess->handler, yajl_allow_multiple_values, 1);
	yajl_config(sess->handler, yajl_allow_trailing_garbage, 1);

	return 0;

err_yajl_handler:
	topic_decref(sess->topic_handler);

topic_err:
	return -1;
}

void free_zz_session(struct zz_session *sess) {
	string_done(&sess->http_prev_chunk.last_object);
	yajl_free(sess->handler);
	topic_decref(sess->topic_handler);
}
