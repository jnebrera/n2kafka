/*
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
** Copyright (C) 2018-2019, Wizzie S.L.
** Author: Eugenio Perez <eupm90@gmail.com>
**
** This file is part of n2kafka.
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

#include <microhttpd.h>

#include <string.h>

/**
 * @brief      Sends a buffered response to the client
 *
 * @param      con            The connector to send response
 * @param[in]  sz             The buffer size
 * @param      buf            The buffer
 * @param[in]  buf_kind       The buffer kind (see microhttpd)
 * @param[in]  response_code  The HTTP response code
 *
 * @return     0 if it could be queued, !0 otherwise.
 */
int send_buffered_response(struct MHD_Connection *con,
			   size_t sz,
			   char *buf,
			   int buf_kind,
			   unsigned int response_code);

/**
 * @brief      Sends a http 200 ok
 *
 * @param      connection  The connection
 *
 * @return     Same as `send_buffered_response`
 */
int send_http_ok(struct MHD_Connection *connection);

/**
 * @brief      Sends a http 405 method not allowed, and explicitly allows HTTP
 *             GET and POST methods.
 *
 * @param      connection  The connection
 *
 * @return     Same as `send_buffered_response`
 */
int send_http_method_not_allowed_allow_get_post(
		struct MHD_Connection *connection);

/**
 * @brief      Sends a http 405 method not allowed, and explicitly allows HTTP
 *             POST methods.
 *
 * @param      connection  The connection
 *
 * @return     Same as `send_buffered_response`
 */
int send_http_method_not_allowed_allow_post(struct MHD_Connection *connection);

/**
 * @brief      Sends a http 401 unauthorized, and tell that basic authentication
 *             is allowed
 *
 * @param      connection  The connection
 *
 * @return     Same as `send_buffered_response`
 */
int send_http_unauthorized_basic(struct MHD_Connection *connection);

/**
 * @brief      Decrements the pre-allocated static responses reference counter
 */
void responses_listener_counter_decref();

/**
 * @brief      Increments the pre-allocated static responses reference counter
 *
 * @return     0 if allocation success, !0 otherwise
 */
int responses_listener_counter_incref();
