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

#include "zz_http2k_parser.h"

#include "zz_database.h"

#include "util/pair.h"
#include "util/topic_database.h"
#include "util/util.h"

#include <librd/rdlog.h>

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

struct zz_session *zz_session_cast(void *opaque) {
	struct zz_session *ret = opaque;
#ifdef ZZ_SESSION_MAGIC
	assert(ZZ_SESSION_MAGIC == ret->magic);
#endif
	return ret;
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

/**
 * @brief      Checks if a content type is xml
 *
 * @param[in]  content_type  The content type
 *
 * @return     True if xml content type, False otherwise.
 */
static bool is_xml_content_type(const char *content_type) {
	if (!content_type) {
		return false;
	}

	const size_t content_type_len = strlen(content_type);
	if (content_type_len < strlen("xml")) {
		return false;
	}

	return 0 == strcasecmp(content_type + content_type_len - strlen("xml"),
			       "xml");
}

int new_zz_session(struct zz_session *sess,
		   struct zz_database *zz_db,
		   const keyval_list_t *msg_vars) {
	assert(sess);
	assert(zz_db);
	assert(msg_vars);
	const char *client_ip = valueof(msg_vars, "D-Client-IP", strcmp);
	const char *url = valueof(msg_vars, "D-HTTP-URI", strcmp);
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
	client_uuid.buf = valueof(msg_vars, "X-Consumer-ID", strcasecmp);

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

	const char *content_type =
			valueof(msg_vars, "Content-type", strcasecmp);
	const bool content_type_xml = is_xml_content_type(content_type);

	const int handler_rc = content_type_xml ? new_zz_session_xml(sess)
						: new_zz_session_json(sess);

	if (unlikely(0 != handler_rc)) {
		goto err_handler;
	}

	return 0;

err_handler:
	topic_decref(sess->topic_handler);

topic_err:
	return -1;
}

void free_zz_session(struct zz_session *sess) {
	string_done(&sess->http_response);

	sess->free_session(sess);

	topic_decref(sess->topic_handler);
}
