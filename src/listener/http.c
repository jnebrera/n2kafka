/*
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
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

#include "config.h"

#ifdef HAVE_LIBMICROHTTPD

#define HTTP_UNUSED __attribute__((unused))

#define MODE_THREAD_PER_CONNECTION "thread_per_connection"
#define MODE_SELECT "select"
#define MODE_POLL "poll"
#define MODE_EPOLL "epoll"

#include "engine/rb_addr.h"
#include "http.h"
#include "util/topic_database.h"

#include "engine/global_config.h"

#include <arpa/inet.h>
#include <assert.h>
#include <jansson.h>
#include <librd/rdlog.h>
#include <librd/rdmem.h>
#include <math.h>
#include <microhttpd.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/// Initial string to start with
#define STRING_INITIAL_SIZE 2048

/// Chunk to store decompression flow
#define ZLIB_CHUNK (512 * 1024)

struct string {
	char *buf;
	size_t allocated, used;
};

#define HTTP_PRIVATE_MAGIC 0xC0B345FE

/// Connection private data
struct http_private {
#ifdef HTTP_PRIVATE_MAGIC
	/// Casting magic
	uint64_t magic;
#endif
	/// Associated daemon
	struct MHD_Daemon *d;

	/// Callback associated with received data
	decoder_callback callback;

	/// Opaque to send to callback
	void *callback_opaque;

	/// Callback flags
	int callback_flags;
};

static size_t smax(size_t n1, size_t n2) {
	return n1 > n2 ? n1 : n2;
}

static size_t smin(size_t n1, size_t n2) {
	return n1 > n2 ? n2 : n1;
}

static int init_string(struct string *s, size_t size) {
	s->buf = malloc(size);
	if (s->buf) {
		s->allocated = size;
		return 1;
	}
	return 0;
}

static size_t string_free_space(const struct string *str) {
	return str->allocated - str->used;
}

static size_t string_grow(struct string *str, size_t delta) {
	const size_t newsize = smax(str->allocated + delta, str->allocated * 2);
	char *new_buf = realloc(str->buf, newsize);
	if (NULL != new_buf) {
		str->buf = new_buf;
		str->allocated = newsize;
	}
	return str->allocated;
}

/// Per connection information
struct conn_info {
	/// Per connection string
	struct string str;
	/// Decoders parameters
	/// @todo no need for a linked list, it's better with a pair array
	keyval_list_t decoder_params;

	/// libz related
	struct {
		/// Request has asked for compressed data
		int enable;
		/// zlib handler
		z_stream strm;
	} zlib;

	/// Session pointer.
	void *decoder_sessp;

	/// Number of decoder options
	size_t decoder_opts_size;

	/// Memory pool for decoder_params
	struct pair decoder_opts[0];
};

static void free_con_info(struct conn_info *con_info) {
	free(con_info->str.buf);
	con_info->str.buf = NULL;
	free(con_info);
}

static void request_completed(void *cls,
			      struct MHD_Connection *connection HTTP_UNUSED,
			      void **con_cls,
			      enum MHD_RequestTerminationCode toe) {
	if (NULL == con_cls || NULL == *con_cls) {
		return; /* This point should never reached? */
	}

	if (toe != MHD_REQUEST_TERMINATED_COMPLETED_OK) {
		rdlog(LOG_ERR, "Connection terminated because %d", toe);
	}

	struct conn_info *con_info = *con_cls;
	struct http_private *h = cls;
#ifdef HTTP_PRIVATE_MAGIC
	assert(HTTP_PRIVATE_MAGIC == h->magic);
#endif

	if (!h->callback_flags & DECODER_F_SUPPORT_STREAMING) {
		/* No streaming processing -> need to process buffer */
		h->callback(con_info->str.buf,
			    con_info->str.used,
			    &con_info->decoder_params,
			    h->callback_opaque,
			    NULL);
		con_info->str.buf = NULL; /* librdkafka will free it */
	} else {
		/* Streaming processing -> need to free session pointer */
		h->callback(NULL,
			    0,
			    &con_info->decoder_params,
			    h->callback_opaque,
			    &con_info->decoder_sessp);
	}

	if (con_info->zlib.enable) {
		inflateEnd(&con_info->zlib.strm);
	}

	free_con_info(con_info);
	*con_cls = NULL;
}

static int connection_args_iterator(void *cls,
				    enum MHD_ValueKind kind,
				    const char *key,
				    const char *value) {
	struct conn_info *con_info = cls;
	const size_t i = con_info->decoder_opts_size++;

	if (kind != MHD_HEADER_KIND) {
		return MHD_YES; // Not interested in
	}

	if (key && value && 0 == strcmp("Content-Encoding", key) &&
	    0 == strcmp("deflate", value)) {
		con_info->zlib.enable = 1;
	}

	con_info->decoder_opts[i].key = key;
	con_info->decoder_opts[i].value = value;

	return MHD_YES; // keep iterating
}

static const char *zlib_error2str(const int z_status) {
	switch (z_status) {
	case Z_OK:
		return "success";
		break;
	case Z_MEM_ERROR:
		return "there was not enough memory";
		break;
	case Z_VERSION_ERROR:
		return "the zlib library version is incompatible with the"
		       " version assumed by the caller";
		break;
	case Z_STREAM_ERROR:
		return "the parameters are invalid";
		break;
	default:
		return "Unknown error";
		break;
	};
}

static struct conn_info *
create_connection_info(size_t string_size,
		       const char *uri,
		       const char *client,
		       struct MHD_Connection *connection) {

	/* First call, creating all needed structs */
	const int num_post_headers =
			MHD_get_connection_values(connection,
						  MHD_HEADER_KIND,
						  /* iterator */ NULL,
						  /* iterator opaque */ NULL);
	const size_t num_decoder_opts =
			(size_t)num_post_headers + 2 /* URI & client IP */;
	struct conn_info *con_info = NULL;

	const size_t con_info_size =
			sizeof(*con_info) +
			num_decoder_opts * sizeof(con_info->decoder_opts[0]);
	rd_calloc_struct(&con_info,
			 con_info_size,
			 client ? -1 : 0,
			 client,
			 &con_info->decoder_opts[0].value,
			 RD_MEM_END_TOKEN);

	if (unlikely(NULL == con_info)) {
		rdlog(LOG_ERR,
		      "Can't allocate conection context (out of memory?)");
		return NULL; /* Doesn't have resources */
	}

	con_info->decoder_opts[0].key = "D-Client-IP";
	con_info->decoder_opts[1].key = "D-HTTP-URI";
	con_info->decoder_opts[1].value = uri;
	con_info->decoder_opts_size = 2;

	MHD_get_connection_values(connection,
				  MHD_HEADER_KIND,
				  connection_args_iterator,
				  con_info);

	keyval_list_init(&con_info->decoder_params);
	size_t i;
	for (i = 0; i < con_info->decoder_opts_size; ++i) {
		add_key_value_pair(&con_info->decoder_params,
				   &con_info->decoder_opts[i]);
	}

	/// @todo delay this! it could be not needed!
	if (!init_string(&con_info->str, string_size)) {
		rdlog(LOG_ERR,
		      "Can't allocate connection string buffer (out "
		      "of memory?)");
		free_con_info(con_info);
		return NULL; /* Doesn't have resources */
	}

	if (con_info->zlib.enable) {
		con_info->zlib.strm.zalloc = Z_NULL;
		con_info->zlib.strm.zfree = Z_NULL;
		con_info->zlib.strm.opaque = Z_NULL;
		con_info->zlib.strm.avail_in = 0;
		con_info->zlib.strm.next_in = Z_NULL;

		const int rc = inflateInit(&con_info->zlib.strm);
		if (rc != Z_OK) {
			rdlog(LOG_ERR,
			      "Couldn't init inflate. Error was %d: %s",
			      rc,
			      zlib_error2str(rc));
		}
	}

	return con_info;
}

static int
send_buffered_response(struct MHD_Connection *con,
		       size_t sz,
		       char *buf,
		       int buf_kind,
		       unsigned int response_code,
		       int (*custom_response)(struct MHD_Response *)) {
	struct MHD_Response *http_response =
			MHD_create_response_from_buffer(sz, buf, buf_kind);

	if (NULL == http_response) {
		rdlog(LOG_CRIT, "Can't create HTTP response");
		return MHD_NO;
	}

	if (custom_response) {
		custom_response(http_response);
	}

	const int ret = MHD_queue_response(con, response_code, http_response);
	MHD_destroy_response(http_response);
	return ret;
}

static int send_http_ok(struct MHD_Connection *connection) {
	return send_buffered_response(connection,
				      0,
				      NULL,
				      MHD_RESPMEM_PERSISTENT,
				      MHD_HTTP_OK,
				      NULL);
}

static int
customize_method_not_allowed_response(struct MHD_Response *response) {
	return MHD_add_response_header(response, "Allow", "POST");
}

static int send_http_method_not_allowed(struct MHD_Connection *connection) {
	return send_buffered_response(connection,
				      0,
				      NULL,
				      MHD_RESPMEM_PERSISTENT,
				      MHD_HTTP_METHOD_NOT_ALLOWED,
				      customize_method_not_allowed_response);
}

static int send_http_bad_request(struct MHD_Connection *connection) {
	return send_buffered_response(connection,
				      0,
				      NULL,
				      MHD_RESPMEM_PERSISTENT,
				      MHD_HTTP_BAD_REQUEST,
				      NULL);
}

static size_t append_http_data_to_connection_data(struct conn_info *con_info,
						  const char *upload_data,
						  size_t upload_data_size) {

	if (upload_data_size > string_free_space(&con_info->str)) {
		/* TODO error handling */
		string_grow(&con_info->str, upload_data_size);
	}

	size_t ncopy = smin(upload_data_size,
			    string_free_space(&con_info->str));
	strncpy(&con_info->str.buf[con_info->str.used], upload_data, ncopy);
	con_info->str.used += ncopy;
	return ncopy;
}

static const char *
client_addr(char *buf, size_t buf_size, struct MHD_Connection *con_info) {
	const union MHD_ConnectionInfo *cinfo = MHD_get_connection_info(
			con_info, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
	if (NULL == cinfo || NULL == cinfo->client_addr) {
		rdlog(LOG_WARNING,
		      "Can't obtain client address info to print "
		      "debug message.");
		return NULL;
	}

	return sockaddr2str(buf, buf_size, cinfo->client_addr);
}

static size_t compressed_callback(struct http_private *cls,
				  struct conn_info *con_info,
				  const char *upload_data,
				  size_t upload_data_size) {
	static pthread_mutex_t last_zlib_warning_timestamp_mutex =
			PTHREAD_MUTEX_INITIALIZER;
	time_t last_zlib_warning_timestamp = 0;
	size_t rc = 0;

	/* Ugly hack, assignement discard const qualifier */
	memcpy(&con_info->zlib.strm.next_in, &upload_data, sizeof(upload_data));
	con_info->zlib.strm.avail_in = upload_data_size;

	unsigned char *buffer = malloc(ZLIB_CHUNK);

	/* run inflate until output buffer not full */
	do {
		/* Reset counters */
		con_info->zlib.strm.next_out = buffer;
		con_info->zlib.strm.avail_out = ZLIB_CHUNK;

		const int ret = inflate(
				&con_info->zlib.strm,
				Z_NO_FLUSH /* TODO compare different flush */);
		if (ret != Z_OK && ret != Z_STREAM_END) {
			static const time_t threshold_s = 5 * 60;

			pthread_mutex_lock(&last_zlib_warning_timestamp_mutex);
			const time_t now = time(NULL);
			const int warn = difftime(now,
						  last_zlib_warning_timestamp) >
					 threshold_s;
			pthread_mutex_unlock(
					&last_zlib_warning_timestamp_mutex);

			if (warn) {
				const char *client_ip =
						con_info->decoder_opts[0].value;
				switch (ret) {
				case Z_STREAM_ERROR:
					rdlog(LOG_ERR,
					      "Input from ip %s is not a valid "
					      "zlib stream",
					      client_ip);
					break;

				case Z_NEED_DICT:
					rdlog(LOG_ERR,
					      "Need unkown dict in input "
					      "stream from ip %s",
					      client_ip);
					break;

				case Z_DATA_ERROR:
					rdlog(LOG_ERR,
					      "Error in compressed input from "
					      "ip %s",
					      client_ip);
					break;

				case Z_MEM_ERROR:
					rdlog(LOG_ERR,
					      "Memory error, couldn't allocate "
					      "memory for ip %s",
					      client_ip);
					break;

				case Z_OK:
				case Z_STREAM_END:
					/* All ok, keep working */
					break;

				default:
					rdlog(LOG_ERR,
					      "Unknown error: inflate returned "
					      "%d for %s ip",
					      ret,
					      client_ip);
					break;
				};
			}

			inflateEnd(&con_info->zlib.strm);
			break; // @TODO send HTTP error!
		}

		const size_t zprocessed =
				ZLIB_CHUNK - con_info->zlib.strm.avail_out;
		rc += zprocessed; // @TODO this should be returned by callback
				  // call
		if (zprocessed > 0) {
			cls->callback((char *)buffer,
				      zprocessed,
				      &con_info->decoder_params,
				      cls->callback_opaque,
				      &con_info->decoder_sessp);
		}
	} while (con_info->zlib.strm.avail_out == 0);

	/* Do not want to waste memory */
	free(buffer);
	con_info->zlib.strm.next_out = NULL;

	return upload_data_size - con_info->zlib.strm.avail_in;
}

static int post_handle(void *_cls,
		       struct MHD_Connection *connection,
		       const char *url,
		       const char *method,
		       const char *version HTTP_UNUSED,
		       const char *upload_data,
		       size_t *upload_data_size,
		       void **ptr) {
	struct http_private *cls = _cls;
#ifdef HTTP_PRIVATE_MAGIC
	assert(HTTP_PRIVATE_MAGIC == cls->magic);
#endif

	if (0 != strcmp(method, MHD_HTTP_METHOD_POST)) {
		rdlog(LOG_WARNING,
		      "Received invalid method %s. "
		      "Returning METHOD NOT ALLOWED.",
		      method);
		return send_http_method_not_allowed(connection);
	}

	if (NULL == ptr) {
		return MHD_NO;
	}

	if (NULL == *ptr) {
		char client_buf[BUFSIZ];
		const char *client = client_addr(
				client_buf, sizeof(client_buf), connection);
		if (NULL == client) {
			return MHD_NO;
		}

		*ptr = create_connection_info(
				STRING_INITIAL_SIZE, url, client, connection);

		return (NULL == *ptr) ? MHD_NO : MHD_YES;
	} else if (*upload_data_size > 0) {
		/* middle calls, process string sent */
		struct conn_info *con_info = *ptr;
		size_t rc;
		if (!cls->callback_flags & DECODER_F_SUPPORT_STREAMING) {
			/* Does not support stream, we need to allocate a big
			 * buffer */
			rc = append_http_data_to_connection_data(
					con_info,
					upload_data,
					*upload_data_size);
		} else if (con_info->zlib.enable) {
			/* Does support streaming, we will decompress & process
			 * until */
			/* end of received chunk */
			rc = compressed_callback(cls,
						 con_info,
						 upload_data,
						 *upload_data_size);
		} else {
			/* Does support streaming processing, sending the chunk
			 */
			cls->callback(upload_data,
				      *upload_data_size,
				      &con_info->decoder_params,
				      cls->callback_opaque,
				      &con_info->decoder_sessp);
			/// @TODO fix it
			if (0 /* ERROR */) {
			}
			rc = *upload_data_size;
		}
		(*upload_data_size) -= rc;
		return (*upload_data_size != 0) ? MHD_NO : MHD_YES;

	} else {
		/* Send OK. Resources will be freed in request_completed */
		return send_http_ok(connection);
	}
}

struct http_loop_args {
	const char *mode;
	int port;
	int num_threads;
	struct {
		int connection_memory_limit;
		int connection_limit;
		int connection_timeout;
		int per_ip_connection_limit;
	} server_parameters;
};

static struct http_private *start_http_loop(const struct http_loop_args *args,
					    decoder_callback callback,
					    int callback_flags,
					    void *cb_opaque) {
	struct http_private *h = NULL;

	unsigned int flags = 0;
	if (args->mode == NULL ||
	    0 == strcmp(MODE_THREAD_PER_CONNECTION, args->mode)) {
		flags |= MHD_USE_THREAD_PER_CONNECTION;
	} else if (0 == strcmp(MODE_SELECT, args->mode)) {
		flags |= MHD_USE_SELECT_INTERNALLY;
	} else if (0 == strcmp(MODE_POLL, args->mode)) {
		flags |= MHD_USE_POLL_INTERNALLY;
	} else if (0 == strcmp(MODE_EPOLL, args->mode)) {
		flags |= MHD_USE_EPOLL_INTERNALLY_LINUX_ONLY;
	} else {
		rdlog(LOG_ERR,
		      "Not a valid HTTP mode. Select one "
		      "between(" MODE_THREAD_PER_CONNECTION "," MODE_SELECT
		      "," MODE_POLL "," MODE_EPOLL ")");
		return NULL;
	}

	flags |= MHD_USE_DEBUG;

	h = calloc(1, sizeof(*h));
	if (!h) {
		rdlog(LOG_ERR,
		      "Can't allocate LIBMICROHTTPD private"
		      " (out of memory?)");
		return NULL;
	}
#ifdef HTTP_PRIVATE_MAGIC
	h->magic = HTTP_PRIVATE_MAGIC;
#endif
	h->callback = callback;
	h->callback_flags = callback_flags;
	h->callback_opaque = cb_opaque;

	struct MHD_OptionItem opts[] = {
			{MHD_OPTION_NOTIFY_COMPLETED,
			 (intptr_t)&request_completed,
			 h},

			/* Digest-Authentication related. Setting to 0 saves
			   some memory */
			{MHD_OPTION_NONCE_NC_SIZE, 0, NULL},

			/* Max number of concurrent onnections */
			{MHD_OPTION_CONNECTION_LIMIT,
			 args->server_parameters.connection_limit,
			 NULL},

			/* Max number of connections per IP */
			{MHD_OPTION_PER_IP_CONNECTION_LIMIT,
			 args->server_parameters.per_ip_connection_limit,
			 NULL},

			/* Connection timeout */
			{MHD_OPTION_CONNECTION_TIMEOUT,
			 args->server_parameters.connection_timeout,
			 NULL},

			/* Memory limit per connection */
			{MHD_OPTION_CONNECTION_MEMORY_LIMIT,
			 args->server_parameters.connection_memory_limit,
			 NULL},

			/* Thread pool size */
			{MHD_OPTION_THREAD_POOL_SIZE, args->num_threads, NULL},

			{MHD_OPTION_END, 0, NULL}};

	h->d = MHD_start_daemon(flags,
				args->port,
				NULL,	/* Auth callback */
				NULL,	/* Auth callback parameter */
				post_handle, /* Request handler */
				h,	   /* Request handler parameter */
				MHD_OPTION_ARRAY,
				opts,
				MHD_OPTION_END);

	if (NULL == h->d) {
		rdlog(LOG_ERR,
		      "Can't allocate LIBMICROHTTPD handler"
		      " (out of memory?)");
		free(h);
		return NULL;
	}

	return h;
}

static void reload_listener_http(json_t *new_config,
				 decoder_listener_opaque_reload opaque_reload,
				 void *cb_opaque,
				 void *_private __attribute__((unused))) {
	if (opaque_reload) {
		rdlog(LOG_INFO, "Reloading opaque");
		opaque_reload(new_config, cb_opaque);
	} else {
		rdlog(LOG_INFO, "Not reload opaque provided");
	}
}

static void break_http_loop(void *_h) {
	struct http_private *h = _h;
	MHD_stop_daemon(h->d);
	free(h);
}

struct listener *create_http_listener(struct json_t *config,
				      decoder_callback cb,
				      int cb_flags,
				      void *cb_opaque) {

	json_error_t error;

	struct http_loop_args handler_args;
	memset(&handler_args, 0, sizeof(handler_args));

	/* Default arguments */

	handler_args.num_threads = 1;
	handler_args.mode = MODE_SELECT;
	handler_args.server_parameters.connection_memory_limit = 128 * 1024;
	handler_args.server_parameters.connection_limit = 1024;
	handler_args.server_parameters.connection_timeout = 30;
	handler_args.server_parameters.per_ip_connection_limit = 0;

	/* Unpacking */

	const int unpack_rc = json_unpack_ex(
			config,
			&error,
			0,
			"{"
			"s:i," /* port */
			"s?s," /* mode */
			"s?i," /* num_threads */
			"s?i," /* connection_memory_limit */
			"s?i," /* connection_limit */
			"s?i," /* connection_timeout */
			"s?i"  /* per_ip_connection_limit */
			"}",
			"port",
			&handler_args.port,
			"mode",
			&handler_args.mode,
			"num_threads",
			&handler_args.num_threads,
			"connection_memory_limit",
			&handler_args.server_parameters.connection_memory_limit,
			"connection_limit",
			&handler_args.server_parameters.connection_limit,
			"connection_timeout",
			&handler_args.server_parameters.connection_timeout,
			"per_ip_connection_limit",
			&handler_args.server_parameters
					 .per_ip_connection_limit);

	if (unpack_rc != 0 /* Failure */) {
		rdlog(LOG_ERR, "Can't parse HTTP options: %s", error.text);
		return NULL;
	}

	struct http_private *priv =
			start_http_loop(&handler_args, cb, cb_flags, cb_opaque);
	if (NULL == priv) {
		return NULL;
	}

	struct listener *listener = calloc(1, sizeof(*listener));
	if (!listener) {
		rdlog(LOG_ERR, "Can't create http listener (out of memory?)");
		free(priv);
		return NULL;
	}

	rdlog(LOG_INFO,
	      "Creating new HTTP listener on port %d",
	      handler_args.port);

	listener->create = create_http_listener;
	listener->cb.cb_opaque = cb_opaque;
	listener->cb.callback = cb;
	listener->join = break_http_loop;
	listener->private = priv;
	listener->reload = reload_listener_http;
	listener->port = handler_args.port;

	return listener;
}

#endif
