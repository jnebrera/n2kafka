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

#pragma once

#include <curl/curl.h>
#include <librd/rdqueue.h>
#include <pthread.h>

/** CURL handler to send PUT messages */
typedef struct zz_http2k_curl_handler_s {
#ifndef NDEBUG
#define RB_HTTP2K_CURL_HANDLER_MAGIC 0xB112C3A1CB112C3AL
	uint64_t magic; ///< Magic to assert coherence
#endif
	volatile int run;	  ///< Keep running
	pthread_t thread;	  ///< Thread handler
	rd_fifoq_t msg_queue;      ///< MSG queue
	CURLM *curl_multi_handler; ///< Curl handler
} zz_http2k_curl_handler_t;

/** Creates a zz_http2k_curl_handler
  @param handler Handler to start
  @param max_msgs_size Maximum number of messages to accept
  @return 0 if success, !0 in other case
  */
int zz_http2k_curl_handler_init(zz_http2k_curl_handler_t *handler,
				int max_msgs_size);

/** Free zz_curl handler resources
  @param handler Handler to be freed
  */
void zz_http2k_curl_handler_done(zz_http2k_curl_handler_t *handler);

/** Send an empty PUT to a given URL
  @param handler Handler to use to send PUT
  @param URL URL to send it
  @return 0 if OK, ENOBUFS if queue is full
  */
void zz_http2k_curl_handler_put_empty(zz_http2k_curl_handler_t *handler,
				      const char *url);
