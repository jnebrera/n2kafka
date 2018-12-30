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

#pragma once

#ifdef HAVE_LIBMICROHTTPD

#include "config.h"

#include <microhttpd.h>

#include <stddef.h>

struct n2k_decoder;
struct json_t;

/// Per listener stuff
struct http_listener;

#define HTTP_UNUSED __attribute__((unused))

/**
 * @brief      Cast http listener to a plain listener
 *
 * @param      listener  The HTTP listener
 *
 * @return     The listener
 */
const struct listener *
http_listener_cast_listener(const struct http_listener *listener);

/**
 * @brief      Cast a void pointer to an actual http_listener, aborting if
 * error.
 *
 * @param      vhttp_listener  The voit pointer to http listener
 *
 * @return     The http listener
 */
struct http_listener *http_listener_cast(void *vhttp_listener);

/**
 * @brief      Returns the http decoder name
 *
 * @return     HTTP decoder name
 */
const char *http_name();

/**
 * @brief      HTTP listener internal callbacks
 */
struct http_callbacks {
	/// Request handling
	int (*handle_request)(void *vhttp_listener,
			      struct MHD_Connection *connection,
			      const char *url,
			      const char *method,
			      const char *version,
			      const char *upload_data,
			      size_t *upload_data_size,
			      void **ptr);

	/// Request completed
	void (*request_completed)(void *cls,
				  struct MHD_Connection *connection HTTP_UNUSED,
				  void **con_cls,
				  enum MHD_RequestTerminationCode toe);
};

/**
 * @brief      Creates a http listener.
 *
 * @param[in]  http_callbacks  The http callbacks
 * @param[in]  t_config        The listener configuration
 * @param[in]  decoder         The decoder to use
 *
 * @return     New listener or NULL in case of failure.
 */
struct listener *
create_http_listener0(const struct http_callbacks *http_callbacks,
		      const struct json_t *t_config,
		      const struct n2k_decoder *decoder);

#endif // HAVE_LIBMICROHTTPD
