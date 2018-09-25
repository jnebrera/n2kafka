/*
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
** Copyright (C) 2018, Wizzie S.L.
** Author: Eugenio Perez <eupm90@gmail.com>
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

#include "responses.h"

#include <librd/rdlog.h>

static struct {
	struct MHD_Response *empty_response;
	struct MHD_Response *method_not_allowed_allow_get_post;
	struct MHD_Response *method_not_allowed_allow_post;
	int listeners_counter;
} http_responses;

int send_buffered_response(struct MHD_Connection *con,
			   size_t sz,
			   char *buf,
			   int buf_kind,
			   unsigned int response_code) {
	struct MHD_Response *http_response =
			MHD_create_response_from_buffer(sz, buf, buf_kind);

	if (NULL == http_response) {
		rdlog(LOG_CRIT, "Can't create HTTP response");
		return MHD_NO;
	}

	const int ret = MHD_queue_response(con, response_code, http_response);
	MHD_destroy_response(http_response);
	return ret;
}

int send_http_ok(struct MHD_Connection *connection) {
	return MHD_queue_response(
			connection, MHD_HTTP_OK, http_responses.empty_response);
}

/**
 * @brief      Sends a HTTP 405 method not allowed.
 *
 * @param      connection  The connection
 * @param      response    The response
 *
 * @return     Same as `send_buffered_response`
 */
static int send_http_method_not_allowed(struct MHD_Connection *connection,
					struct MHD_Response *response) {
	return MHD_queue_response(
			connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
}

int send_http_method_not_allowed_allow_get_post(
		struct MHD_Connection *connection) {
	return send_http_method_not_allowed(
			connection,
			http_responses.method_not_allowed_allow_get_post);
}

int send_http_method_not_allowed_allow_post(struct MHD_Connection *connection) {
	return send_http_method_not_allowed(
			connection,
			http_responses.method_not_allowed_allow_post);
}

void responses_listener_counter_decref() {
	if (0 == --http_responses.listeners_counter) {
		MHD_destroy_response(http_responses.empty_response);
		MHD_destroy_response(
				http_responses.method_not_allowed_allow_get_post);
		MHD_destroy_response(
				http_responses.method_not_allowed_allow_post);
	}
}

int responses_listener_counter_incref() {
	if (0 == http_responses.listeners_counter++) {
		http_responses.empty_response = MHD_create_response_from_buffer(
				0, NULL, MHD_RESPMEM_PERSISTENT);
		http_responses.method_not_allowed_allow_get_post =
				MHD_create_response_from_buffer(
						0,
						NULL,
						MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header(
				http_responses.method_not_allowed_allow_get_post,
				"Allow",
				"GET, POST");

		http_responses.method_not_allowed_allow_post =
				MHD_create_response_from_buffer(
						0,
						NULL,
						MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header(
				http_responses.method_not_allowed_allow_post,
				"Allow",
				"POST");
	}

	return 0;
}
