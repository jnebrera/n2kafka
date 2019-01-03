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

#include "http_auth.h"
#include "http_config.h"
#include "responses.h"

#include "listener/listener_api.h"

#include "util/file.h"
#include "util/util.h"

#include <jansson.h>
#include <librd/rd.h>
#include <librd/rdfile.h>
#include <librd/rdlog.h>
#include <microhttpd.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#define MODE_THREAD_PER_CONNECTION "thread_per_connection"
#define MODE_SELECT "select"
#define MODE_POLL "poll"
#define MODE_EPOLL "epoll"

/// Per listener stuff
struct http_listener {
	// Note: This MUST be the first member!
	struct listener listener; ///< listener
#ifndef NDEBUG
#define HTTP_PRIVATE_MAGIC 0xC0B345FE
	uint64_t magic; ///< magic field
#endif
	size_t tls_data_size;
	struct MHD_Daemon *d; ///< Associated daemon
	char *htpasswd;
	bool client_tls_cert;
	char tls_data[];
};

const struct listener *
http_listener_cast_listener(const struct http_listener *hlistener) {
	return &hlistener->listener;
}

struct http_listener *http_listener_cast(void *vhttp_listener) {
	struct http_listener *http_listener = vhttp_listener;
#ifdef HTTP_PRIVATE_MAGIC
	assert(HTTP_PRIVATE_MAGIC == http_listener->magic);
#endif
	return http_listener;
}

bool http_listener_config_client_tls_ca(const struct http_listener *l) {
	return l->client_tls_cert;
}

const char *http_listener_htpasswd(const struct http_listener *l) {
	return l->htpasswd;
}

/**
 * @brief      Wrapper for htpassword database size
 *
 * @param      f     File to extract the data
 *
 * @return     The htpasswd database needed size
 */
static off64_t htpasswd_size(FILE *f) {
	const ssize_t rc = http_auth_extract_data(NULL, 0, f);
	if (rc < 0) {
		return 0;
	}
	return (off64_t)rc;
}

/**
 * @brief      Wrapper for htpasswd database data extraction
 *
 * @param      dst     The destination database
 * @param[in]  size    The database size, in bytes
 * @param[in]  count   The count of elements. Use 1 if you want coherency.
 * @param      stream  The file to extract htpasswd.
 *
 * @return     Number of bytes size/count unities read from stream.
 */
static size_t
htpasswd_fread(void *dst, size_t size, size_t count, FILE *stream) {
	const ssize_t data_read =
			http_auth_extract_data(dst, size * count, stream);
	if (data_read < 0) {
		return 0;
	}

	return (size_t)data_read / size;
}

/**
 * @brief      Delete the HTTP listener in-memory tls data
 *
 * @param      l     Listener
 */
static void http_listener_scrub_tls_data(struct http_listener *l) {
	volatile void *r = memset(l->tls_data, 0, l->tls_data_size);
	(void)r;
}

/**
 * @brief      Return the same string
 *
 * @param[in]  a        the argument and return string
 *
 * @return     Same pointer as a
 */
static const char *string_identity_function(const char *a) {
	return a;
}

// X(struct_type, json_unpack_type, json_name, struct_name, env_name,
// str_to_value_function, default)
#define X_HTTP_CONFIG(X)                                                       \
	/* HTTP server port */                                                 \
	X(int, ":i", port, port, NULL, atoi, 0)                                \
	/* HTTP server number of polling threads */                            \
	X(int, "?i", num_threads, num_threads, NULL, atoi, 1)                  \
	/* Per connection memory limit */                                      \
	X(int,                                                                 \
	  "?i",                                                                \
	  connection_memory_limit,                                             \
	  connection_memory_limit,                                             \
	  NULL,                                                                \
	  atoi,                                                                \
	  (128 * 1024))                                                        \
	/* Connections limit */                                                \
	X(int, "?i", connection_limit, connection_limit, NULL, atoi, 1024)     \
	/* Timeout to drop a connection */                                     \
	X(int, "?i", connection_timeout, connection_timeout, NULL, atoi, 30)   \
	/* Per-ip connection limit */                                          \
	X(int,                                                                 \
	  "?i",                                                                \
	  per_ip_connection_limit,                                             \
	  per_ip_connection_limit,                                             \
	  NULL,                                                                \
	  atoi,                                                                \
	  0)                                                                   \
	/* Polling mode: thread_per_connection, select, poll (default), epoll  \
	 */                                                                    \
	X(const char *,                                                        \
	  "?s",                                                                \
	  mode,                                                                \
	  mode,                                                                \
	  NULL,                                                                \
	  string_identity_function,                                            \
	  "poll")                                                              \
	/* Server TLS key filename */                                          \
	X(const char *,                                                        \
	  "?s",                                                                \
	  https_key_filename,                                                  \
	  https_key_filename,                                                  \
	  "HTTP_TLS_KEY_FILE",                                                 \
	  string_identity_function,                                            \
	  NULL)                                                                \
	/* Server TLS key file password */                                     \
	X(const char *,                                                        \
	  "?s",                                                                \
	  https_cert_filename,                                                 \
	  https_cert_filename,                                                 \
	  "HTTP_TLS_CERT_FILE",                                                \
	  string_identity_function,                                            \
	  NULL)                                                                \
	/* Server TLS key file password */                                     \
	X(const char *,                                                        \
	  "?s",                                                                \
	  https_key_password,                                                  \
	  https_key_password,                                                  \
	  "HTTP_TLS_KEY_PASSWORD",                                             \
	  string_identity_function,                                            \
	  NULL)                                                                \
	/* HTTPS clients recognized CA */                                      \
	X(const char *,                                                        \
	  "?s",                                                                \
	  https_clients_ca_filename,                                           \
	  https_clients_ca_filename,                                           \
	  "HTTP_TLS_CLIENT_CA_FILE",                                           \
	  string_identity_function,                                            \
	  NULL)                                                                \
	/* htpasswd file */                                                    \
	X(const char *,                                                        \
	  "?s",                                                                \
	  htpasswd_filename,                                                   \
	  htpasswd_filename,                                                   \
	  "HTTP_HTPASSWD_FILE",                                                \
	  string_identity_function,                                            \
	  NULL)

#define X_STRUCT_HTTP_CONFIG(                                                  \
		struct_type, json_unpack_type, json_name, struct_name, ...)    \
	struct_type struct_name;

/**
 * @brief      HTTP listener loop arguments
 */
struct http_loop_args {
	X_HTTP_CONFIG(X_STRUCT_HTTP_CONFIG)
};

/**
 * @brief      Delete all HTTP loop data
 *
 * @param      vhttp_listener  The void pointer to listener listener
 */
static void break_http_loop(struct listener *vhttp_listener) {
	struct http_listener *http_listener =
			(struct http_listener *)vhttp_listener;

#ifdef HTTP_PRIVATE_MAGIC
	assert(HTTP_PRIVATE_MAGIC == http_listener->magic);
#endif
	MHD_stop_daemon(http_listener->d);
	listener_join(&http_listener->listener);
	if (http_listener->tls_data_size > 0) {
		http_listener_scrub_tls_data(http_listener);
		munlock(http_listener->tls_data, http_listener->tls_data_size);
	}
	free(http_listener);

	responses_listener_counter_decref();
}

/**
 * @brief      Start a http server
 *
 * @param[in]  args            The arguments
 * @param[in]  http_callbacks  The http callbacks
 * @param[in]  decoder         The decoder
 * @param[in]  decoder_conf    The decoder config
 *
 * @return     New listener, NULL in case of error
 */
static struct http_listener *
start_http_loop(const struct http_loop_args *args,
		const struct http_callbacks *http_callbacks,
		const struct n2k_decoder *decoder,
		const json_t *decoder_conf) {
	struct http_listener *http_listener = NULL;
	unsigned int flags = 0;
	enum secret_files {
		KEY_FILE,
		CERT_FILE,
		CLIENT_CA_TRUST,
		HTPASSWD,
	};

	// clang-format off
	struct {
		const char *filename; ///< Filename to get certs from
		const char *log_kind; ///< Kind of file for logging purposes
		FILE *file;           ///< File pointer for resource handling
		/// Callback to extract file size
		off64_t (*file_size_callback)(FILE *stream);
		/// File content reading callback. Fread signature
		size_t (*file_read_callback)(void* ptr,
			                     size_t size,
		                             size_t count,
		                             FILE *stream);
		size_t filesize;      ///< File size
		char *mem;            ///< Raw memory
	} secret_files[] = {
		[KEY_FILE] = {
			.log_kind = "Private TLS key",
			.file_size_callback = file_size,
			.file_read_callback = fread,
			.filename = args->https_key_filename,
		},
		[CERT_FILE] = {
			.log_kind = "Exposed TLS certificate",
			.file_size_callback = file_size,
			.file_read_callback = fread,
			.filename = args->https_cert_filename,
		},
		[CLIENT_CA_TRUST] = {
			.log_kind = "Client TLS certificate Authority",
			.file_size_callback = file_size,
			.file_read_callback = fread,
			.filename =
			      args->https_clients_ca_filename,
		},
		[HTPASSWD] = {
			.log_kind = "htpasswd file",
			.file_size_callback = htpasswd_size,
			.file_read_callback = htpasswd_fread,
			.filename = args->htpasswd_filename,
		}
	};
	// clang-format on

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

	if (unlikely(!(args->https_key_filename) !=
		     !(args->https_cert_filename))) {
		// User set only one of the two
		static const char *key_hint = "https_key_filename or "
					      "HTTP_TLS_KEY_FILE environ";
		static const char *cert_hint = "https_cert_filename or "
					       "HTTP_TLS_CERT_FILE environ";

		rdlog(LOG_ERR,
		      "Only %s set in http listener options, you must also set "
		      "%s ",
		      args->https_key_filename ? key_hint : cert_hint,
		      args->https_key_filename ? cert_hint : key_hint);
		return NULL;
	}

	if (unlikely(!(args->https_key_filename) &&
		     args->https_clients_ca_filename)) {
		// User set only one of the two
		rdlog(LOG_ERR,
		      "`https_clients_ca` set in http listener options, you "
		      "must also set `https_key_filename`");
		return NULL;
	}

	if (args->https_key_filename) {
		flags |= MHD_USE_TLS;
	}

	for (size_t i = 0; (flags & MHD_USE_TLS || args->htpasswd_filename) &&
			   i < RD_ARRAYSIZE(secret_files);
	     ++i) {
		if (!secret_files[i].filename) {
			continue;
		}

		if (i == KEY_FILE) {
			struct stat private_key_stat;
			const int stat_rc = stat(secret_files[i].filename,
						 &private_key_stat);

			if (unlikely(stat_rc != 0)) {
				rdlog(LOG_ERR,
				      "Can't get information of file "
				      "\"%s\": %s",
				      secret_files[i].filename,
				      gnu_strerror_r(errno));
				goto tls_err;
			}

			if (unlikely((private_key_stat.st_mode &
				      (S_IWOTH | S_IROTH)))) {
				rdlog(LOG_ERR,
				      "\"Others\" can read Key file "
				      "\"%s\" Please fix the file "
				      "permissions before try to start "
				      "this http server",
				      secret_files[i].filename);
				goto tls_err;
			}
		}

		secret_files[i].file = fopen(secret_files[i].filename, "rb");

		if (unlikely(secret_files[i].file == NULL)) {
			rdlog(LOG_ERR,
			      "Can't open %s file: %s",
			      secret_files[i].filename,
			      gnu_strerror_r(errno));

			goto tls_err;
		}

		// MHD uses strlen directly, so we need to make room for
		// the \0 terminator
		const off64_t t_file_size = secret_files[i].file_size_callback(
				secret_files[i].file);
		if (unlikely(t_file_size == (off64_t)-1)) {
			rdlog(LOG_ERR,
			      "Couldn't get \"%s\" file size: %s",
			      secret_files[i].filename,
			      gnu_strerror_r(errno));
			goto tls_err;
		}

		secret_files[i].filesize = (size_t)t_file_size;
	}

	// clang-format off
	const size_t tls_files_size =
		(flags & MHD_USE_TLS ||
			 args->htpasswd_filename)
		? ({
			size_t s = 0;
			for (size_t i = 0; i < RD_ARRAYSIZE(secret_files);
				                                          ++i) {
				if (NULL == secret_files[i].filename) {
					continue;
				}
				// NULL terminator needed for MHD
				s += secret_files[i].filesize + 1;
			  }
			  s;
		  })
		: 0;
	// clang-format on

	http_listener = calloc(1,
			       sizeof(*http_listener) + (size_t)tls_files_size);
	if (!http_listener) {
		rdlog(LOG_ERR,
		      "Can't allocate LIBMICROHTTPD private"
		      " (out of memory?)");
		return NULL;
	}

	if (flags & MHD_USE_TLS || args->htpasswd_filename) {
		if (args->https_clients_ca_filename) {
			http_listener->client_tls_cert = true;
		}

		// Certificates and private key processing
		http_listener->tls_data_size = tls_files_size;
		char *cursor = http_listener->tls_data;
		for (size_t i = 0; i < RD_ARRAYSIZE(secret_files); ++i) {
			if (NULL == secret_files[i].filename) {
				continue;
			}

			rdlog(LOG_INFO,
			      "Reading file %s as %s",
			      secret_files[i].filename,
			      secret_files[i].log_kind);

			secret_files[i].mem = cursor;
			if (i == KEY_FILE) {
				// Try to avoid disk swapping of private key
				const int mlock_rc =
						mlock(secret_files[i].mem,
						      secret_files[i].filesize);

				if (unlikely(0 != mlock_rc)) {
					rdlog(LOG_WARNING,
					      "Can lock private key on RAM "
					      "memory. It could be swapped on "
					      "disk.");
				}
			}

			const size_t readed = secret_files[i].file_read_callback(
					cursor,
					1,
					secret_files[i].filesize,
					secret_files[i].file);

			if (unlikely(readed < secret_files[i].filesize)) {
				rdlog(LOG_ERR,
				      "Can't read %s file",
				      secret_files[i].filename);

				goto tls_err;
			}

			cursor += (size_t)secret_files[i].filesize + 1;
		}

		assert(cursor ==
		       http_listener->tls_data + http_listener->tls_data_size);
	}

	if (args->htpasswd_filename) {
		http_listener->htpasswd = secret_files[HTPASSWD].mem;
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

	responses_listener_counter_incref();
	const struct MHD_OptionItem opts[] = {
			{MHD_OPTION_NOTIFY_COMPLETED,
			 (intptr_t)http_callbacks->request_completed,
			 http_listener},

			/* Digest-Authentication related. Setting to 0
			   saves some memory */
			{MHD_OPTION_NONCE_NC_SIZE, 0, NULL},

			/* Max number of concurrent onnections */
			{MHD_OPTION_CONNECTION_LIMIT,
			 args->connection_limit,
			 NULL},

			/* Max number of connections per IP */
			{MHD_OPTION_PER_IP_CONNECTION_LIMIT,
			 args->per_ip_connection_limit,
			 NULL},

			/* Connection timeout */
			{MHD_OPTION_CONNECTION_TIMEOUT,
			 args->connection_timeout,
			 NULL},

			/* Memory limit per connection */
			{MHD_OPTION_CONNECTION_MEMORY_LIMIT,
			 args->connection_memory_limit,
			 NULL},

			/* Thread pool size */
			{MHD_OPTION_THREAD_POOL_SIZE, args->num_threads, NULL},

			/* Finish options OR https tls options */
			{flags & MHD_USE_TLS ? MHD_OPTION_HTTPS_MEM_KEY
					     : MHD_OPTION_END,
			 0,
			 secret_files[KEY_FILE].mem},
			{MHD_OPTION_HTTPS_MEM_CERT,
			 0,
			 secret_files[CERT_FILE].mem},
			{MHD_OPTION_HTTPS_KEY_PASSWORD,
			 0,
			 const_cast(args->https_key_password)},

			/* Finish options OR HTTPS client CA */
			{secret_files[CLIENT_CA_TRUST].filename
					 ? MHD_OPTION_HTTPS_MEM_TRUST
					 : MHD_OPTION_END,
			 0,
			 secret_files[CLIENT_CA_TRUST].mem},

			{MHD_OPTION_END, 0, NULL}};

	http_listener->d = MHD_start_daemon(
			flags,
			args->port,
			NULL, /* Auth callback */
			NULL, /* Auth callback parameter */
			http_callbacks->handle_request, /* Request
							   handler
							 */
			http_listener,			/* Request handler
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

tls_err:
	for (size_t i = 0; i < RD_ARRAYSIZE(secret_files); ++i) {
		if (secret_files[i].file) {
			fclose(secret_files[i].file);
		}
	}

	return http_listener;

start_daemon_err:
	responses_listener_counter_decref();
	http_listener->listener.join(&http_listener->listener);

listener_init_err:
	// Volatile avoid write-before-free optimization!
	if (http_listener->tls_data_size > 0) {
		http_listener_scrub_tls_data(http_listener);
		munlock(http_listener->tls_data, http_listener->tls_data_size);
	}
	free(http_listener);
	return NULL;
}

struct listener *create_http_listener0(const struct http_callbacks *callbacks,
				       const struct json_t *t_config,
				       const struct n2k_decoder *decoder) {
	struct http_listener *http_listener = NULL;
	int key_password_len = 0;
	char *key_password = NULL;
	json_t *config = json_deep_copy(t_config);
	if (NULL == config) {
		rdlog(LOG_ERR, "Couldn't dup config (OOM?)");
		return NULL;
	}
	json_error_t error;

#define X_DEFAULTS_HTTP_CONFIG(struct_type,                                    \
			       json_unpack_type,                               \
			       json_name,                                      \
			       struct_name,                                    \
			       env_name,                                       \
			       str_to_value_function,                          \
			       t_default)                                      \
	.struct_name = t_default,

#define X_ENVIRON_HTTP_CONFIG(struct_type,                                     \
			      json_unpack_type,                                \
			      json_name,                                       \
			      struct_name,                                     \
			      env_name,                                        \
			      str_to_value_function,                           \
			      ...)                                             \
	if (NULL != env_name) {                                                \
		const char *env_val = getenv(env_name);                        \
		if (env_val) {                                                 \
			handler_args.struct_name =                             \
					str_to_value_function(env_val);        \
		}                                                              \
	}

#define X_JANSSON_UNPACK_FORMAT(struct_type, json_unpack_type, ...)            \
	"s" json_unpack_type

#define X_JANSSON_UNPACK_ARGS(                                                 \
		struct_type, json_unpack_type, json_name, struct_name, ...)    \
	/* */ #json_name, &handler_args.struct_name,

	// Configured defaults
	struct http_loop_args handler_args = {
			X_HTTP_CONFIG(X_DEFAULTS_HTTP_CONFIG)};

	// Override with envionment
	X_HTTP_CONFIG(X_ENVIRON_HTTP_CONFIG)

	// Override with config file
	const int unpack_rc = json_unpack_ex(
			config,
			&error,
			0,
			"{" X_HTTP_CONFIG(X_JANSSON_UNPACK_FORMAT) "}",
			X_HTTP_CONFIG(X_JANSSON_UNPACK_ARGS)
			/* unused NULL, only to make C syntax happy*/ NULL);

	if (unpack_rc != 0 /* Failure */) {
		rdlog(LOG_ERR, "Can't parse HTTP options: %s", error.text);
		goto err;
	}

	if (handler_args.https_key_password &&
	    handler_args.https_key_password[0] == '@') {
		// Key password is in a file
		key_password = rd_file_read(&handler_args.https_key_password[1],
					    &key_password_len);
		handler_args.https_key_password = key_password;
	}

	http_listener = start_http_loop(
			&handler_args, callbacks, decoder, config);
	if (NULL == http_listener) {
		rdlog(LOG_ERR, "Can't create http listener (out of memory?)");
		goto err;
	}

	rdlog(LOG_INFO,
	      "Creating new HTTP listener on port %d",
	      handler_args.port);

err:
	json_decref(config);
	if (key_password) {
		// scrub data
		volatile void *r = memset(
				key_password, 0, (size_t)key_password_len);
		(void)r;
		free(key_password);
	}
	return http_listener ? &http_listener->listener : NULL;
}

const char *http_name() {
	return "http";
}

#endif // HAVE_LIBMICROHTTPD
