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

#include "config.h"

#ifdef HAVE_LIBMICROHTTPD

#define HTTP_UNUSED __attribute__((unused))

#define MODE_THREAD_PER_CONNECTION "thread_per_connection"
#define MODE_SELECT "select"
#define MODE_POLL "poll"
#define MODE_EPOLL "epoll"

#include "engine/rb_addr.h"
#include "http.h"
#include "util/string.h"
#include "util/topic_database.h"

#include "engine/global_config.h"

#include <librd/rdlog.h>
#include <librd/rdmem.h>
#include <microhttpd.h>
#include <zlib.h>

#include <arpa/inet.h>
#include <assert.h>
#include <jansson.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/// Initial string to start with
#define STRING_INITIAL_SIZE 2048

/// Chunk to store decompression flow
#define ZLIB_CHUNK (512 * 1024)

/// Per listener stuff
struct http_listener {
	// Note: This MUST be the first member!
	struct listener listener; ///< listener
#ifndef NDEBUG
#define HTTP_PRIVATE_MAGIC 0xC0B345FE
	uint64_t magic; ///< magic field
#endif
	struct MHD_Daemon *d; ///< Associated daemon
};

static struct {
	struct MHD_Response *empty_response;
	struct MHD_Response *method_not_allowed;
	int listeners_counter;
} http_responses;

static struct http_listener *http_listener_cast(void *vhttp_listener) {
	struct http_listener *http_listener = vhttp_listener;
#ifdef HTTP_PRIVATE_MAGIC
	assert(HTTP_PRIVATE_MAGIC == http_listener->magic);
#endif
	return http_listener;
}

/// Per connection information
struct conn_info {
	/// Per connection string
	string str;
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

	/// pre-allocated session pointer.
	void *decoder_sess;

	/// Number of decoder options
	size_t decoder_opts_size;

	/// Memory pool for decoder_params
	struct pair decoder_opts[];
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

	assert(con_cls);
	if (NULL == *con_cls) {
		return; /* This point should never reached? */
	}

	if (toe != MHD_REQUEST_TERMINATED_COMPLETED_OK) {
		rdlog(LOG_ERR, "Connection terminated because %d", toe);
	}

	struct conn_info *con_info = *con_cls;
	struct http_listener *http_listener = http_listener_cast(cls);
	const struct n2k_decoder *decoder = http_listener->listener.decoder;

	if (!decoder->new_session) {
		/* No streaming processing -> need to process buffer */
		listener_decode(&http_listener->listener,
				con_info->str.buf,
				con_info->str.size,
				&con_info->decoder_params,
				NULL,
				NULL,
				NULL);
	} else if (decoder->delete_session) {
		/* Streaming processing -> need to free session pointer */
		decoder->delete_session(con_info->decoder_sess);
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

	assert(key);

	if (kind != MHD_HEADER_KIND) {
		return MHD_YES; // Not interested in
	}

	if (value && 0 == strcmp("Content-Encoding", key) &&
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

/// Save all decoder options in conn_info, or ask for size if !conn_info
static size_t decoder_opts(struct MHD_Connection *connection,
			   const char *http_method,
			   const char *uri,
			   const char *client,
			   struct conn_info *conn_info) {
	const struct pair listener_options[] = {
			{.key = "D-HTTP-method", .value = http_method},
			{.key = "D-HTTP-URI", .value = uri},
			{.key = "D-Client-IP", .value = client},
	};

	const size_t num_http_headers = (size_t)MHD_get_connection_values(
			connection,
			MHD_HEADER_KIND,
			conn_info ? connection_args_iterator : NULL,
			conn_info);

	if (conn_info) {
		memcpy(&conn_info->decoder_opts[conn_info->decoder_opts_size],
		       listener_options,
		       sizeof(listener_options));

		conn_info->decoder_opts_size += RD_ARRAYSIZE(listener_options);

		keyval_list_init(&conn_info->decoder_params);
		for (size_t i = 0; i < conn_info->decoder_opts_size; ++i) {
			add_key_value_pair(&conn_info->decoder_params,
					   &conn_info->decoder_opts[i]);
		}
	}

	return num_http_headers + RD_ARRAYSIZE(listener_options);
}

static struct conn_info *
create_connection_info(const char *http_method,
		       const char *uri,
		       const char *client,
		       const size_t decoder_session_size,
		       struct MHD_Connection *connection) {

	/* First call, creating all needed structs */
	const size_t num_decoder_opts = decoder_opts(
			connection, http_method, uri, client, NULL);
	struct conn_info *con_info = NULL;

	const size_t con_info_size =
			sizeof(*con_info) +
			num_decoder_opts * sizeof(con_info->decoder_opts[0]) +
			decoder_session_size;
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

	if (decoder_session_size) {
		con_info->decoder_sess =
				&con_info->decoder_opts[num_decoder_opts];
	}

	decoder_opts(connection, http_method, uri, client, con_info);

	string_init(&con_info->str);

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

static int send_buffered_response(struct MHD_Connection *con,
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

static int send_http_ok(struct MHD_Connection *connection) {
	return MHD_queue_response(
			connection, MHD_HTTP_OK, http_responses.empty_response);
}

static int send_http_method_not_allowed(struct MHD_Connection *connection) {
	return MHD_queue_response(connection,
				  MHD_HTTP_METHOD_NOT_ALLOWED,
				  http_responses.method_not_allowed);
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

static size_t compressed_callback(struct http_listener *h_listener,
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
					      "Input from ip %s is not "
					      "a valid "
					      "zlib stream",
					      client_ip);
					break;

				case Z_NEED_DICT:
					rdlog(LOG_ERR,
					      "Need unkown dict in "
					      "input "
					      "stream from ip %s",
					      client_ip);
					break;

				case Z_DATA_ERROR:
					rdlog(LOG_ERR,
					      "Error in compressed "
					      "input from "
					      "ip %s",
					      client_ip);
					break;

				case Z_MEM_ERROR:
					rdlog(LOG_ERR,
					      "Memory error, couldn't "
					      "allocate "
					      "memory for ip %s",
					      client_ip);
					break;

				case Z_OK:
				case Z_STREAM_END:
					/* All ok, keep working */
					break;

				default:
					rdlog(LOG_ERR,
					      "Unknown error: inflate "
					      "returned "
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
		rc += zprocessed; // @TODO this should be returned by
				  // callback call
		if (zprocessed > 0) {
			listener_decode(&h_listener->listener,
					(char *)buffer,
					zprocessed,
					&con_info->decoder_params,
					NULL,
					NULL,
					con_info->decoder_sess);
		}
	} while (con_info->zlib.strm.avail_out == 0);

	/* Do not want to waste memory */
	free(buffer);
	con_info->zlib.strm.next_out = NULL;

	return upload_data_size - con_info->zlib.strm.avail_in;
}

static int handle_post(void *vhttp_listener,
		       struct MHD_Connection *connection,
		       const char *url,
		       const char *method,
		       const char *version HTTP_UNUSED,
		       const char *upload_data,
		       size_t *upload_data_size,
		       void **ptr) {
	struct http_listener *http_listener =
			http_listener_cast(vhttp_listener);

	assert(ptr);

	if (NULL == *ptr) {
		char client_buf[BUFSIZ];
		const char *client = client_addr(
				client_buf, sizeof(client_buf), connection);
		if (unlikely(NULL == client)) {
			return MHD_NO;
		}

		const n2k_decoder *decoder = http_listener->listener.decoder;
		const size_t decoder_session_size =
				decoder->session_size ? decoder->session_size()
						      : 0;

		*ptr = create_connection_info(method,
					      url,
					      client,
					      decoder_session_size,
					      connection);
		if (unlikely(NULL == *ptr)) {
			return MHD_NO;
		}

		if (decoder->new_session) {
			struct conn_info *con_info = *ptr;
			const int session_rc = decoder->new_session(
					con_info->decoder_sess,
					http_listener->listener.decoder_opaque,
					&con_info->decoder_params);
			if (0 != session_rc) {
				// Not valid decoder session!
				free_con_info(con_info);
				*ptr = NULL;
			}
		}

		return (NULL == *ptr) ? MHD_NO : MHD_YES;
	} else if (*upload_data_size > 0) {
		/* middle calls, process string sent */
		const struct n2k_decoder *decoder =
				http_listener->listener.decoder;
		struct conn_info *con_info = *ptr;
		size_t rc;
		if (!decoder->new_session) {
			// Does not support stream, we need to allocate
			// a big buffer and send all the data together
			const int append_rc = string_append(&con_info->str,
							    upload_data,
							    *upload_data_size);
			rc = (0 == append_rc) ? *upload_data_size : 0;
		} else if (con_info->zlib.enable) {
			/* Does support streaming, we will decompress &
			 * process
			 * until */
			/* end of received chunk */
			rc = compressed_callback(http_listener,
						 con_info,
						 upload_data,
						 *upload_data_size);
		} else {
			/* Does support streaming processing, sending
			 * the chunk
			 */
			listener_decode(&http_listener->listener,
					upload_data,
					*upload_data_size,
					&con_info->decoder_params,
					NULL,
					NULL,
					con_info->decoder_sess);
			/// @TODO fix it
			if (0 /* ERROR */) {
			}
			rc = *upload_data_size;
		}
		(*upload_data_size) -= rc;
		return (*upload_data_size != 0) ? MHD_NO : MHD_YES;

	} else {
		/* Send OK. Resources will be freed in request_completed
		 */
		return send_http_ok(connection);
	}
}

static void *const_cast(const void *arg) {
	void *ret;
	memcpy(&ret, &arg, sizeof(ret));
	return ret;
}

static int handle_get(void *vhttp_listener,
		      struct MHD_Connection *connection,
		      const char *uri,
		      const char *method,
		      const char *version HTTP_UNUSED,
		      const char *upload_data,
		      size_t *upload_data_size,
		      void **ptr) {
	if (NULL == *ptr) {
		// If we queue response now, MHD close the transport connection,
		// forbidding HTTP pipelining. Wait until next call.
		*ptr = (void *)1;
		return MHD_YES;
	}

	struct http_listener *http_listener =
			http_listener_cast(vhttp_listener);
	char client_buf[BUFSIZ];
	const char *client =
			client_addr(client_buf, sizeof(client_buf), connection);

	/* First call, creating all needed structs */
	const size_t num_decoder_opts =
			decoder_opts(connection, method, uri, client, NULL);

	struct conn_info *con_info = NULL;
	const size_t con_info_size =
			sizeof(con_info[0]) +
			num_decoder_opts * sizeof(con_info->decoder_opts[0]);
	con_info = alloca(con_info_size);
	memset(con_info, 0, con_info_size);

	decoder_opts(connection, method, uri, client, con_info);

	const char *response = NULL;
	size_t response_size = 0;
	listener_decode(&http_listener->listener,
			upload_data,
			*upload_data_size,
			&con_info->decoder_params,
			&response,
			&response_size,
			con_info->decoder_sess);

	*ptr = NULL;

	return send_buffered_response(connection,
				      response_size,
				      const_cast(response),
				      MHD_RESPMEM_PERSISTENT,
				      MHD_HTTP_OK);
}

static int handle_request(void *vhttp_listener,
			  struct MHD_Connection *connection,
			  const char *url,
			  const char *method,
			  const char *version,
			  const char *upload_data,
			  size_t *upload_data_size,
			  void **ptr) {
	if (0 == strcmp(method, MHD_HTTP_METHOD_POST)) {
		return handle_post(vhttp_listener,
				   connection,
				   url,
				   method,
				   version,
				   upload_data,
				   upload_data_size,
				   ptr);
	} else if (0 == strcmp(method, MHD_HTTP_METHOD_GET)) {
		return handle_get(vhttp_listener,
				  connection,
				  url,
				  method,
				  version,
				  upload_data,
				  upload_data_size,
				  ptr);
	} else {
		rdlog(LOG_WARNING,
		      "Received invalid method %s. "
		      "Returning METHOD NOT ALLOWED.",
		      method);
		return send_http_method_not_allowed(connection);
	}
}

static void break_http_loop(struct listener *vhttp_listener) {
	struct http_listener *http_listener =
			(struct http_listener *)vhttp_listener;

#ifdef HTTP_PRIVATE_MAGIC
	assert(HTTP_PRIVATE_MAGIC == http_listener->magic);
#endif
	MHD_stop_daemon(http_listener->d);
	listener_join(&http_listener->listener);
	free(http_listener);

	if (0 == --http_responses.listeners_counter) {
		MHD_destroy_response(http_responses.empty_response);
		MHD_destroy_response(http_responses.method_not_allowed);
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

static struct http_listener *start_http_loop(const struct http_loop_args *args,
					     const struct n2k_decoder *decoder,
					     const json_t *decoder_conf) {
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

	struct http_listener *http_listener = calloc(1, sizeof(*http_listener));
	if (!http_listener) {
		rdlog(LOG_ERR,
		      "Can't allocate LIBMICROHTTPD private"
		      " (out of memory?)");
		return NULL;
	}

	const int listener_init_rc = listener_init(&http_listener->listener,
						   args->port,
						   decoder,
						   decoder_conf);

	if (0 != listener_init_rc) {
		goto listener_init_err;
	}

#ifdef HTTP_PRIVATE_MAGIC
	http_listener->magic = HTTP_PRIVATE_MAGIC;
#endif

	const struct MHD_OptionItem opts[] = {
			{MHD_OPTION_NOTIFY_COMPLETED,
			 (intptr_t)&request_completed,
			 http_listener},

			/* Digest-Authentication related. Setting to 0
			   saves
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

	http_listener->d = MHD_start_daemon(flags,
					    args->port,
					    NULL, /* Auth callback */
					    NULL, /* Auth callback parameter */
					    handle_request, /* Request handler
							     */
					    http_listener,  /* Request handler
							       parameter */
					    MHD_OPTION_ARRAY,
					    opts,
					    MHD_OPTION_END);

	if (NULL == http_listener->d) {
		rdlog(LOG_ERR,
		      "Can't allocate LIBMICROHTTPD handler"
		      " (out of memory?)");
		goto start_daemon_err;
	}

	http_listener->listener.join = break_http_loop;
	return http_listener;

start_daemon_err:
	http_listener->listener.join(&http_listener->listener);

listener_init_err:
	free(http_listener);
	return NULL;
}

/*
  FACTORY
*/

static struct listener *
create_http_listener(const struct json_t *t_config,
		     const struct n2k_decoder *decoder) {
	json_t *config = json_deep_copy(t_config);
	if (NULL == config) {
		rdlog(LOG_ERR, "Couldn't dup config (OOM?)");
		return NULL;
	}
	json_error_t error;

	if (0 == http_responses.listeners_counter++) {
		http_responses.empty_response = MHD_create_response_from_buffer(
				0, NULL, MHD_RESPMEM_PERSISTENT);
		http_responses.method_not_allowed =
				MHD_create_response_from_buffer(
						0,
						NULL,
						MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header(http_responses.method_not_allowed,
					"Allow",
					"GET, POST");
	}

	struct http_loop_args handler_args = {
			/* Default arguments */
			.num_threads = 1,
			.mode = MODE_SELECT,
			.server_parameters = {
					.connection_memory_limit = 128 * 1024,
					.connection_limit = 1024,
					.connection_timeout = 30,
					.per_ip_connection_limit = 0,
			}};

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
		goto err;
	}

	struct http_listener *http_listener =
			start_http_loop(&handler_args, decoder, config);
	if (NULL == http_listener) {
		rdlog(LOG_ERR, "Can't create http listener (out of memory?)");
		goto err;
	}

	rdlog(LOG_INFO,
	      "Creating new HTTP listener on port %d",
	      handler_args.port);

err:
	json_decref(config);
	return http_listener ? &http_listener->listener : NULL;
}

static const char *http_name() {
	return "http";
}

const n2k_listener_factory http_listener_factory = {
		.name = http_name,
		.create = create_http_listener,
};

#endif
