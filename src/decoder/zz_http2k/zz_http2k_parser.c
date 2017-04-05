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

/*
    PARSING & ENRICHMENT
*/

#define GEN_AND_RETURN(func)                                                   \
	do {                                                                   \
		return yajl_gen_status_ok == func;                             \
	} while (0);

static int zz_parse_null(void *ctx) {
	struct zz_session *sess = ctx;
	yajl_gen g = sess->gen;

	GEN_AND_RETURN(yajl_gen_null(g));
}

static int zz_parse_boolean(void *ctx, int boolean) {
	struct zz_session *sess = ctx;
	yajl_gen g = sess->gen;

	GEN_AND_RETURN(yajl_gen_bool(g, boolean));
}

static int zz_parse_number(void *ctx, const char *s, size_t l) {
	struct zz_session *sess = ctx;
	yajl_gen g = sess->gen;

	GEN_AND_RETURN(yajl_gen_number(g, s, l));
}

static int
zz_parse_string(void *ctx, const unsigned char *stringVal, size_t stringLen) {
	struct zz_session *sess = ctx;
	yajl_gen g = sess->gen;

	GEN_AND_RETURN(yajl_gen_string(g, stringVal, stringLen));
}

static int
zz_parse_map_key(void *ctx, const unsigned char *stringVal, size_t stringLen) {
	struct zz_session *sess = ctx;
	yajl_gen g = sess->gen;

	GEN_AND_RETURN(yajl_gen_string(g, stringVal, stringLen));
}

static int zz_parse_start_map(void *ctx) {
	struct zz_session *sess = ctx;
	yajl_gen g = sess->gen;

	sess->stack_pos++;
	GEN_AND_RETURN(yajl_gen_map_open(g));
}

/** Generate kafka message.
    */
static int zz_parse_generate_rdkafka_message(const struct zz_session *sess,
					     rd_kafka_message_t *msg) {
	const unsigned char *buf;
	memset(msg, 0, sizeof(*msg));

	msg->partition = RD_KAFKA_PARTITION_UA;

	yajl_gen_get_buf(sess->gen, &buf, &msg->len);

	/// @TODO do not copy, steal the buffer!
	msg->payload = strdup((const char *)buf);
	if (NULL == msg->payload) {
		rdlog(LOG_ERR, "Unable to duplicate buffer");
		return -1;
	}

	return 0;
}

static int zz_parse_end_map(void *ctx) {
	struct zz_session *sess = ctx;
	yajl_gen g = sess->gen;

	const int yajl_gen_status_rc = yajl_gen_map_close(g);

	sess->stack_pos--;
	// const unsigned char *buf = NULL;
	// size_t buf_len = 0;

	// yajl_gen_get_buf(g, &buf, &buf_len);
	// rdlog(LOG_ERR, "CLOSING MAP! [rc=%d][msg=%.*s]",
	// 	yajl_gen_status_rc,
	// 	(int)buf_len,
	// 	buf);

	if (0 == sess->stack_pos) {
		rd_kafka_message_t msg;
		/* Ending message */
		if (0 == zz_parse_generate_rdkafka_message(sess, &msg)) {
			rd_kafka_msg_q_add(&sess->msg_queue, &msg);
		}

		yajl_gen_reset(sess->gen, NULL);
		yajl_gen_clear(sess->gen);
	}

	GEN_AND_RETURN(yajl_gen_status_rc);
}

static int zz_parse_start_array(void *ctx) {
	struct zz_session *sess = ctx;
	yajl_gen g = sess->gen;
	GEN_AND_RETURN(yajl_gen_array_open(g));
}

/// @TODO allow array root?
static int zz_parse_end_array(void *ctx) {
	struct zz_session *sess = ctx;
	yajl_gen g = sess->gen;

	GEN_AND_RETURN(yajl_gen_array_close(g));
}

static const yajl_callbacks callbacks = {zz_parse_null,
					 zz_parse_boolean,
					 NULL,
					 NULL,
					 zz_parse_number,
					 zz_parse_string,
					 zz_parse_start_map,
					 zz_parse_map_key,
					 zz_parse_end_map,
					 zz_parse_start_array,
					 zz_parse_end_array};

/* Return code: Valid uri prefix (i.e., /v1/)*/
static int
extract_url_topic(const char *url, const char **topic, size_t *topic_size) {
	assert(url);
	assert(topic);
	assert(topic_size);

	if (unlikely('/' != url[0])) {
		return -1;
	}

	url++; // skip slash
	*topic = strchrnul(url, '/');
	if (unlikely(NULL == *topic)) {
		return -1;
	}

	(*topic)++;
	const int valid_version =
			0 == strncmp(url, "v1/", (size_t)(*topic - url));
	if (unlikely(!valid_version)) {
		return -1;
	}
	*topic_size = strcspn(*topic, ";/?:@=&");
	if (unlikely(*topic_size == 0)) {
		return -1;
	}

	return 0;
}

struct zz_session *
new_zz_session(struct zz_database *zz_db, const keyval_list_t *msg_vars) {
	const char *client_ip = valueof(msg_vars, "D-Client-IP");
	const char *url = valueof(msg_vars, "D-HTTP-URI");
	struct {
		const char *buf;
		size_t buf_len;
	} topic, client_uuid;

	const int parse_url_rc =
			extract_url_topic(url, &topic.buf, &topic.buf_len);
	if (unlikely(0 != parse_url_rc)) {
		rdlog(LOG_ERR, "Couldn't extract url topic from %s", url);
		return NULL;
	}
	client_uuid.buf = valueof(msg_vars, "X-Consumer-ID");

	if (unlikely(NULL == client_uuid.buf)) {
		return NULL;
	}
	client_uuid.buf_len = strlen(client_uuid.buf);

	char uuid_topic[topic.buf_len + sizeof((char)'_') +
			client_uuid.buf_len + sizeof((char)'\0')];
	memcpy(uuid_topic, client_uuid.buf, client_uuid.buf_len);
	uuid_topic[client_uuid.buf_len] = '_';
	memcpy(&uuid_topic[client_uuid.buf_len + sizeof((char)'_')],
	       topic.buf,
	       topic.buf_len);
	uuid_topic[client_uuid.buf_len + sizeof((char)'_') + topic.buf_len] =
			'\0';

	struct zz_session *sess = calloc(1, sizeof(*sess));
	if (unlikely(NULL == sess)) {
		rdlog(LOG_CRIT, "Couldn't allocate sess pointer");
		goto session_err;
	}

	sess->topic_handler = zz_http2k_database_get_topic(
			zz_db, uuid_topic, time(NULL));
	if (unlikely(NULL == sess->topic_handler)) {
		rdlog(LOG_ERR,
		      "Invalid topic %s received from client %s",
		      topic.buf,
		      client_ip);
		goto topic_err;
	}

	rd_kafka_msg_q_init(&sess->msg_queue);

	sess->gen = yajl_gen_alloc(NULL);
	if (NULL == sess->gen) {
		rdlog(LOG_CRIT, "Couldn't allocate yajl_gen");
		goto err_yajl_gen;
	}

	sess->handler = yajl_alloc(&callbacks, NULL, sess);
	if (NULL == sess->handler) {
		rdlog(LOG_CRIT, "Couldn't allocate yajl_handler");
		goto err_yajl_handler;
	}

	yajl_config(sess->handler, yajl_allow_multiple_values, 1);
	yajl_config(sess->handler, yajl_allow_trailing_garbage, 1);

	return sess;

err_yajl_handler:
	yajl_gen_free(sess->gen);

err_yajl_gen:
	topic_decref(sess->topic_handler);

topic_err:
	free(sess);

session_err:
	return NULL;
}

void free_zz_session(struct zz_session *sess) {
	yajl_free(sess->handler);
	yajl_gen_free(sess->gen);

	topic_decref(sess->topic_handler);

	free(sess);
}
