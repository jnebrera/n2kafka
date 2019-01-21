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

#include "config.h"

#ifdef HAVE_LIBMICROHTTPD

#include "http.h"
#include "http_auth.h"
#include "responses.h"

#include "http_config.h"
#include "responses.h"
#include "tls.h"

#include "decoder/decoder_api.h"
#include "engine/rb_addr.h"

#include "util/pair.h"
#include "util/string.h"
#include "util/util.h"

#include <gnutls/gnutls.h>
#include <jansson.h>
#include <librd/rd.h>
#include <librd/rdlog.h>
#include <librd/rdmem.h>
#include <microhttpd.h>

#include <alloca.h>
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <zlib.h>

/// Chunk to store decompression flow
#define ZLIB_CHUNK (512 * 1024)

#define HTTP_UNUSED __attribute__((unused))

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

	/// HTTP error
	struct {
		unsigned int code; ///< HTTP error code. If !0, further request
				   ///< chunks will not be processed.
		const char *str;   ///< Error string to return. Can be NULL.
		size_t str_size;   ///< String size. If NULL, it will be
				   ///< calculated via strlen(str)
	} http_error;

	/// Number of decoder options
	size_t decoder_opts_size;

	/// Memory pool for decoder_params
	struct pair decoder_opts[];
};

/**
 * @brief      Queue a request response. If called processing post request,
 *             decoder will not be called anymore
 *
 * @param      conn_info                 Connection
 * @param[in]  http_error_code           Http error code to queue
 * @param[in]  http_error_response       Http error response string, if any
 * @param[in]  http_error_response_size  Http error response size
 */
static void conn_info_queue_response(struct conn_info *conn_info,
				     unsigned int http_error_code,
				     const char *http_error_response,
				     size_t http_error_response_size) {
	conn_info->http_error.code = http_error_code;
	conn_info->http_error.str = http_error_response;
	conn_info->http_error.str_size = http_error_response_size;
}

/**
 * @brief      Check if connection has a response queued.
 *
 * @param[in]  conn_info  The connection information
 *
 * @return     True if affirmative
 */
static bool conn_info_has_queue_response(const struct conn_info *conn_info) {
	return 0 != conn_info->http_error.code;
}

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
		rdlog(LOG_ERR, "Connection terminated because %u", toe);
	}

	struct conn_info *con_info = *con_cls;
	struct http_listener *http_listener = http_listener_cast(cls);
	const struct n2k_decoder *decoder =
			http_listener_cast_listener(http_listener)->decoder;

	if (decoder->delete_session) {
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

	if (value && 0 == strcasecmp("Content-Encoding", key) &&
	    (0 == strcasecmp("deflate", value) ||
	     (0 == strcasecmp("gzip", value)))) {
		con_info->zlib.enable = 1;
	}

	con_info->decoder_opts[i].key = key;
	con_info->decoder_opts[i].value = value;

	return MHD_YES; // keep iterating
}

/**
 * @brief      Translate a zlib error code to a static human readable string
 *
 * @param[in]  z_status  zlib error
 *
 * @return     Static allocated human readable description
 */
static const char *zlib_init_error2str(const int z_status) {
	switch (z_status) {
	case Z_MEM_ERROR:
		return "{\"error\":\"Out of memory on zlib init\"}";
	default:
		// case Z_VERSION_ERROR:
		// case Z_OK:
		//
		assert(0);
		return "{\"error\":\"Unknown error\"}";
	};
}

static const char *zlib_deflate_error2str(const int z_status) {
	switch (z_status) {
	// case Z_STREAM_ERROR:
	//	return "Stream structure was inconsistent"
	// case Z_OK:
	// case Z_STREAM_END:
	//	return "No error"
	case Z_NEED_DICT:
		return "{\"error\":\"libz deflate error: a dictionary is "
		       "need\"}";
	case Z_DATA_ERROR:
		return "{\"error\":\"deflated input is not conforming to the "
		       "zlib format\"}";
	case Z_MEM_ERROR:
		return "{\"error\":\"Out of memory\"}";

	case Z_BUF_ERROR:
		return "{\"error\":\"Is not possible to progress in input "
		       "stream\"}";
	default:
		return "{\"error\":\"Unknown error\"}";
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

/**
 * @brief      Creates a connection information.
 *
 * @param[in]  http_method           The http method
 * @param[in]  uri                   The http uri
 * @param[in]  client                The http client
 * @param[in]  decoder_session_size  The decoder session size
 * @param      connection            The MHD connection
 * @param      error                 The error if return is NULL
 *
 * @return     connection information
 */
static struct conn_info *
create_connection_info(const char *http_method,
		       const char *uri,
		       const char *client,
		       const size_t decoder_session_size,
		       struct MHD_Connection *connection,
		       const char **error) {

	static const int WINDOW_BITS = 15;
	static const int ENABLE_ZLIB_GZIP = 32;

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
		*error = "Can't allocate conection context (out of memory?)";
		rdlog(LOG_ERR, "%s", *error);
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

		const int rc = inflateInit2(&con_info->zlib.strm,
					    WINDOW_BITS | ENABLE_ZLIB_GZIP);
		if (rc != Z_OK) {
			*error = zlib_init_error2str(rc);
			rdlog(LOG_ERR,
			      "Couldn't init inflate. Error was %d: %s",
			      rc,
			      *error);
		}
	}

	return con_info;
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

/**
 * @brief      Checks validity for HTTP client certificate
 *
 * @param      connection   The HTTP connection
 * @param[in]  client_addr  The client address, used for debug information.
 *
 * @return     True if the client has a valid certificate, false (and proper
 * response sent to connection) otherwise
 */
static bool http_valid_client_certificate(struct MHD_Connection *connection,
					  const char *client_addr) {
	const char *client_cert_errstr;

	const union MHD_ConnectionInfo *ci = MHD_get_connection_info(
			connection, MHD_CONNECTION_INFO_GNUTLS_SESSION);
	const gnutls_session_t tls_session = ci->tls_session;

	const bool rc = tls_valid_client_certificate(
			tls_session, &client_cert_errstr, client_addr);
	if (likely(rc)) {
		return rc;
	}

	const int send_rc =
			send_buffered_response(connection,
					       strlen(client_cert_errstr),
					       const_cast(client_cert_errstr),
					       MHD_RESPMEM_PERSISTENT,
					       MHD_HTTP_FORBIDDEN);

	if (unlikely(!send_rc)) {
		rdlog(LOG_ERR,
		      "Couldn't queue client \"%s\" 403 HTTP_FORBIDDEN \"%s\""
		      "response (out of memory?)",
		      client_addr,
		      client_cert_errstr);
	}

	return rc;
}

/**
 * @brief      Process compressed POST message
 *
 * @param      h_listener        The http listener
 * @param      con_info          The connection information
 * @param[in]  upload_data       The upload data
 * @param[in]  upload_data_size  The upload data size
 * @param      response          The HTTP response
 * @param      response_size     The HTTP response size
 *
 * @return     MHD_YES if all information was processed.
 */
static enum decoder_callback_err
compressed_callback(struct http_listener *h_listener,
		    struct conn_info *con_info,
		    const char *upload_data,
		    size_t upload_data_size,
		    const char **response,
		    size_t *response_size) {
	static pthread_mutex_t last_zlib_warning_timestamp_mutex =
			PTHREAD_MUTEX_INITIALIZER;
	time_t last_zlib_warning_timestamp = 0;
	enum decoder_callback_err rc = DECODER_CALLBACK_OK;

	con_info->zlib.strm.next_in = const_cast(upload_data);
	con_info->zlib.strm.avail_in = upload_data_size;

	unsigned char *buffer = malloc(ZLIB_CHUNK);

	/* run inflate until output buffer not full */
	do {
		/* Reset counters */
		con_info->zlib.strm.next_out = buffer;
		con_info->zlib.strm.avail_out = ZLIB_CHUNK;

		const int zret = inflate(
				&con_info->zlib.strm,
				Z_NO_FLUSH /* TODO compare different flush */);
		if (unlikely(zret != Z_OK && zret != Z_STREAM_END)) {
			static const time_t threshold_s = 5 * 60;
			// Simulate decoder error
			switch (zret) {
			case Z_NEED_DICT:
			case Z_DATA_ERROR:
				rc = DECODER_CALLBACK_INVALID_REQUEST;
				break;
			default:
				rc = DECODER_CALLBACK_GENERIC_ERROR;
				break;
			}
			*response = zlib_deflate_error2str(zret);

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
				rdlog(LOG_ERR,
				      "Compressed error %d from client %s: %s",
				      zret,
				      client_ip,
				      *response);
			}

			break;
		}

		const size_t zprocessed =
				ZLIB_CHUNK - con_info->zlib.strm.avail_out;

		/// @TODO this should only in case of session decoder!
		if (zprocessed) {
			rc = listener_decode(
					http_listener_cast_listener(h_listener),
					(char *)buffer,
					zprocessed,
					&con_info->decoder_params,
					response,
					response_size,
					con_info->decoder_sess);

			if (unlikely(rc != DECODER_CALLBACK_OK)) {
				break;
			}
		}

	} while (con_info->zlib.strm.avail_out == 0);

	/* Do not want to waste memory */
	free(buffer);
	con_info->zlib.strm.next_out = NULL;

	return rc;
}

/** Initialize an HTTP connection and decoder session
  @param http_listener Used listener
  @param connection MHD connection
  @param url Request URL
  @param method Request method
  @param ptr Request library opaque
  @return MHD daemon return code
  */
static int handle_http_post_init(struct http_listener *http_listener,
				 struct MHD_Connection *connection,
				 const char *url,
				 const char *method,
				 void **ptr) {
	char client_buf[BUFSIZ];
	const char *client =
			client_addr(client_buf, sizeof(client_buf), connection);
	if (unlikely(NULL == client)) {
		return MHD_NO;
	}

	if (http_listener_config_client_tls_ca(http_listener) &&
	    !http_valid_client_certificate(connection, client_buf)) {
		// Log in valid_client_certificate
		return MHD_YES;
	}

	const char *htpasswd = http_listener_htpasswd(http_listener);
	if (htpasswd) {
		char *password = NULL;
		char *user = MHD_basic_auth_get_username_password(connection,
								  &password);

		if (unlikely(!user)) {
			return send_http_unauthorized_basic(connection);
		}

		const bool basic_auth_ok =
				http_authenticate(user, password, htpasswd);

		free(user);
		free(password);

		if (unlikely(!basic_auth_ok)) {
			return send_http_unauthorized_basic(connection);
		}
	}

	const n2k_decoder *decoder =
			http_listener_cast_listener(http_listener)->decoder;
	const size_t decoder_session_size =
			decoder->session_size ? decoder->session_size() : 0;

	const char *create_error = NULL;
	*ptr = create_connection_info(method,
				      url,
				      client,
				      decoder_session_size,
				      connection,
				      &create_error);
	if (unlikely(NULL == *ptr)) {
		return send_buffered_response(connection,
					      strlen(create_error),
					      const_cast(create_error),
					      MHD_RESPMEM_PERSISTENT,
					      MHD_HTTP_INTERNAL_SERVER_ERROR);
	}

	if (decoder->new_session) {
		struct conn_info *con_info = *ptr;
		const int session_rc = decoder->new_session(
				con_info->decoder_sess,
				http_listener_cast_listener(http_listener)
						->decoder_opaque,
				&con_info->decoder_params);
		if (0 != session_rc) {
			// Not valid decoder session!
			free_con_info(con_info);
			*ptr = NULL;
		}
	}

	return (NULL == *ptr) ? MHD_NO : MHD_YES;
}

/**
 * @brief      Transform decoder error code to http code.
 *
 * @param[in]  decode_rc  The decoder return code
 *
 * @return     HTTP response code
 */
static unsigned int decoder_err2http(enum decoder_callback_err decode_rc) {
	switch (decode_rc) {
	case DECODER_CALLBACK_OK:
		return MHD_HTTP_OK;
	case DECODER_CALLBACK_BUFFER_FULL:
		return MHD_HTTP_SERVICE_UNAVAILABLE;

	// Client side errors
	case DECODER_CALLBACK_INVALID_REQUEST:
	case DECODER_CALLBACK_UNKNOWN_TOPIC:
	case DECODER_CALLBACK_UNKNOWN_PARTITION:
		return MHD_HTTP_BAD_REQUEST;

	// Kafka errors - Client side
	case DECODER_CALLBACK_MSG_TOO_LARGE:
		return MHD_HTTP_PAYLOAD_TOO_LARGE;

	// HTTP errors
	case DECODER_CALLBACK_HTTP_METHOD_NOT_ALLOWED:
		return MHD_HTTP_METHOD_NOT_ALLOWED;
	case DECODER_CALLBACK_RESOURCE_NOT_FOUND:
		return MHD_HTTP_NOT_FOUND;
	case DECODER_CALLBACK_MEMORY_ERROR:
	case DECODER_CALLBACK_GENERIC_ERROR:
	default:
		return MHD_HTTP_INTERNAL_SERVER_ERROR;
	};
}

/** Handle a sent chunk

 @param      http_listener     n2k HTTP listener
 @param      upload_data       Request chunk
 @param      upload_data_size  upload_data size
 @param      ptr               Connection library opaque

 @return     MHD library response
*/
static int handle_http_post_chunk(struct http_listener *http_listener,
				  const char *upload_data,
				  size_t *upload_data_size,
				  void **ptr) {
	const struct n2k_decoder *decoder =
			http_listener_cast_listener(http_listener)->decoder;
	struct conn_info *con_info = *ptr;
	const char *response = NULL;
	size_t response_size = 0;
	enum decoder_callback_err decode_rc = 0;
	if (unlikely(conn_info_has_queue_response(con_info))) {
		goto err;
	}

	if (!decoder->new_session) {
		// Does not support stream, we need to allocate
		// a big buffer and send all the data together
		const int append_rc = string_append(
				&con_info->str, upload_data, *upload_data_size);
		decode_rc = (0 == append_rc) ? DECODER_CALLBACK_OK
					     : DECODER_CALLBACK_MEMORY_ERROR;
	} else if (con_info->zlib.enable) {
		// Does support streaming, we will decompress &  process until
		// end of received chunk
		decode_rc = compressed_callback(http_listener,
						con_info,
						upload_data,
						*upload_data_size,
						&response,
						&response_size);
	} else {
		// Does support streaming processing, sending the chunk
		decode_rc = listener_decode(
				http_listener_cast_listener(http_listener),
				upload_data,
				*upload_data_size,
				&con_info->decoder_params,
				&response,
				&response_size,
				con_info->decoder_sess);
	}

	if (unlikely(decode_rc != 0)) {
		const unsigned int http_code = decoder_err2http(decode_rc);
		conn_info_queue_response(
				con_info, http_code, response, response_size);
	}

err:
	*upload_data_size = 0;
	return MHD_YES;
}

/** Handle connection close
  @param http_listener Listener
  @param connection HTTP Connection
  @param ptr Request opaque pointer
  @return MHD information
  */
static int handle_post_end(const struct http_listener *http_listener,
			   struct MHD_Connection *connection,
			   void **ptr) {
	const n2k_decoder *decoder =
			http_listener_cast_listener(http_listener)->decoder;
	struct conn_info *con_info = *ptr;

	if (unlikely(0 != con_info->http_error.code)) {
		// Previously detected error
		const size_t effective_len =
				con_info->http_error.str
						? con_info->http_error.str_size
								  ?: strlen(con_info->http_error
											    .str)
						: 0;
		return send_buffered_response(
				connection,
				effective_len,
				const_cast(con_info->http_error.str),
				MHD_RESPMEM_PERSISTENT,
				con_info->http_error.code);
	}

	if (!decoder->new_session) {
		// No streaming processing -> process entire buffer at this
		// moment
		// @TODO return error
		listener_decode(http_listener_cast_listener(http_listener),
				con_info->str.buf,
				con_info->str.size,
				&con_info->decoder_params,
				NULL,
				NULL,
				NULL);
	}

	send_http_ok(connection);
	return MHD_YES;
}

/** Entrypoint for HTTP POST messages
  @param vhttp_listener n2kafka HTTP listener
  @param connection HTTP Connection
  @param url POST URL
  @param method Always "POST"
  @param version Used HTTP version
  @param upload_data Chunk upload data
  @param upload_data_size Size of upload_data
  @param ptr Request mhd opaque pointer
  @return MHD_YES or MHD_NO to indicate MHD library how to handle connection
  */
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
		return handle_http_post_init(
				http_listener, connection, url, method, ptr);
	} else if (*upload_data_size > 0) {
		return handle_http_post_chunk(http_listener,
					      upload_data,
					      upload_data_size,
					      ptr);
	} else {
		return handle_post_end(http_listener, connection, ptr);
	}
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

	// Next call arrived, mark to finish
	*ptr = NULL;

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
	const enum decoder_callback_err decode_rc = listener_decode(
			http_listener_cast_listener(http_listener),
			upload_data,
			*upload_data_size,
			&con_info->decoder_params,
			&response,
			&response_size,
			con_info->decoder_sess);

	if (decode_rc == DECODER_CALLBACK_HTTP_METHOD_NOT_ALLOWED) {
		// TODO make this more flexible. Currently, the decoder only
		// must accept POST methods, and we only can reach this point
		// via GET
		return send_http_method_not_allowed_allow_post(connection);
	}
	const unsigned int http_code = decoder_err2http(decode_rc);

	return send_buffered_response(connection,
				      response_size,
				      const_cast(response),
				      MHD_RESPMEM_PERSISTENT,
				      http_code);
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
		return send_http_method_not_allowed_allow_get_post(connection);
	}
}
/*
  FACTORY
*/

static struct listener *
create_http_listener(const struct json_t *t_config,
		     const struct n2k_decoder *decoder) {
	static const struct http_callbacks http_callbacks = {
			.handle_request = handle_request,
			.request_completed = request_completed,
	};

	return create_http_listener0(&http_callbacks, t_config, decoder);
}

const n2k_listener_factory http_listener_factory = {
		.name = http_name,
		.create = create_http_listener,
};

#endif
