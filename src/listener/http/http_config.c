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
 * @brief      HTTP listener loop arguments
 */
struct http_loop_args {
	const char *mode; ///< HTTP mode
	int port;	 ///< Port to open http server
	int num_threads;  ///< Number of processing threads
	/// Server parameters
	struct {
		int connection_memory_limit;
		int connection_limit;
		int connection_timeout;
		int per_ip_connection_limit;
		const char *https_key_filename, *https_key_password,
				*https_cert_filename;
	} server_parameters;
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
	enum tls_files {
		KEY_FILE,
		CERT_FILE,
	};

	// clang-format off
	struct {
		const char *filename; ///< Filename to get certs from
		FILE *file;           ///< File pointer for resource handling
		size_t filesize;      ///< File size
		char *mem;            ///< Raw memory
	} tls_files[] = {
		[KEY_FILE] = {
			.filename = args->server_parameters.https_key_filename,
		},
		[CERT_FILE] = {
			.filename = args->server_parameters.https_cert_filename,
		},
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

	if (unlikely(!(args->server_parameters.https_key_filename) !=
		     !(args->server_parameters.https_cert_filename))) {
		// User set only one of the two
		static const char *key_hint = "https_key_filename or "
					      "HTTP_TLS_KEY_FILE environ";
		static const char *cert_hint = "https_cert_filename or "
					       "HTTP_TLS_CERT_FILE environ";

		rdlog(LOG_ERR,
		      "Only %s set in http listener options, you must also set "
		      "%s ",
		      args->server_parameters.https_key_filename ? key_hint
								 : cert_hint,
		      args->server_parameters.https_key_filename ? cert_hint
								 : key_hint);
		return NULL;
	}

	if (args->server_parameters.https_key_filename) {
		flags |= MHD_USE_TLS;

		for (size_t i = 0; i < RD_ARRAYSIZE(tls_files); ++i) {
			if (i == KEY_FILE) {
				struct stat private_key_stat;
				const int stat_rc = stat(tls_files[i].filename,
							 &private_key_stat);

				if (unlikely(stat_rc != 0)) {
					rdlog(LOG_ERR,
					      "Can't get information of file "
					      "\"%s\": %s",
					      tls_files[i].filename,
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
					      tls_files[i].filename);
					goto tls_err;
				}
			}

			tls_files[i].file = fopen(tls_files[i].filename, "rb");

			if (unlikely(tls_files[i].file == NULL)) {
				rdlog(LOG_ERR,
				      "Can't open %s file: %s",
				      tls_files[i].filename,
				      gnu_strerror_r(errno));

				goto tls_err;
			}

			// MHD uses strlen directly, so we need to make room for
			// the \0 terminator
			const off64_t t_file_size =
					file_size(tls_files[i].file);
			if (unlikely(t_file_size == (off64_t)-1)) {
				rdlog(LOG_ERR,
				      "Couldn't get \"%s\" file size: %s",
				      tls_files[i].filename,
				      gnu_strerror_r(errno));
				goto tls_err;
			}

			tls_files[i].filesize = (size_t)t_file_size;
		}
	}

	const size_t tls_files_size = (flags & MHD_USE_TLS) ? ({
		size_t s = 0;
		for (size_t i = 0; i < RD_ARRAYSIZE(tls_files); ++i) {
			// NULL terminator needed for MHD
			s += tls_files[i].filesize + 1;
		}
		s;
	})
							    : 0;

	http_listener = calloc(1,
			       sizeof(*http_listener) + (size_t)tls_files_size);
	if (!http_listener) {
		rdlog(LOG_ERR,
		      "Can't allocate LIBMICROHTTPD private"
		      " (out of memory?)");
		return NULL;
	}

	if (flags & MHD_USE_TLS) {
		// Certificate and private key processing
		// Try to avoid disk swapping of private key
		http_listener->tls_data_size = tls_files_size;
		char *cursor = http_listener->tls_data;
		for (size_t i = 0; i < RD_ARRAYSIZE(tls_files); ++i) {
			tls_files[i].mem = cursor;
			if (i == KEY_FILE) {
				const int mlock_rc =
						mlock(tls_files[i].mem,
						      tls_files[i].filesize);

				if (unlikely(0 != mlock_rc)) {
					rdlog(LOG_WARNING,
					      "Can lock private key on RAM "
					      "memory. It could be swapped on "
					      "disk.");
				}
			}

			const size_t readed = fread(cursor,
						    1,
						    tls_files[i].filesize,
						    tls_files[i].file);

			if (unlikely(readed < tls_files[i].filesize)) {
				rdlog(LOG_ERR,
				      "Can't read %s file",
				      tls_files[i].filename);

				goto tls_err;
			}

			cursor += (size_t)tls_files[i].filesize + 1;
		}

		assert(cursor ==
		       http_listener->tls_data + http_listener->tls_data_size);
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

			/* Finish options OR https tls options */
			{flags & MHD_USE_TLS ? MHD_OPTION_HTTPS_MEM_KEY
					     : MHD_OPTION_END,
			 0,
			 tls_files[KEY_FILE].mem},
			{MHD_OPTION_HTTPS_MEM_CERT,
			 0,
			 tls_files[CERT_FILE].mem},
			{MHD_OPTION_HTTPS_KEY_PASSWORD,
			 0,
			 const_cast(args->server_parameters
						    .https_key_password)},

			{MHD_OPTION_END, 0, NULL}};

	http_listener->d = MHD_start_daemon(
			flags,
			args->port,
			NULL, /* Auth callback */
			NULL, /* Auth callback parameter */
			http_callbacks->handle_request, /* Request handler
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
	for (size_t i = 0; i < RD_ARRAYSIZE(tls_files); ++i) {
		if (tls_files[i].file) {
			fclose(tls_files[i].file);
		}
	}

	return http_listener;

start_daemon_err:
	http_listener->listener.join(&http_listener->listener);
	responses_listener_counter_incref();

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
	int key_password_len = 0;
	char *key_password = NULL;
	json_t *config = json_deep_copy(t_config);
	if (NULL == config) {
		rdlog(LOG_ERR, "Couldn't dup config (OOM?)");
		return NULL;
	}
	json_error_t error;

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

	struct {
		const char *env_var;
		const char **dst;
	} envs_config[] = {
			{.env_var = "HTTP_TLS_KEY_FILE",
			 .dst = &handler_args.server_parameters
						 .https_key_filename},
			{.env_var = "HTTP_TLS_CERT_FILE",
			 .dst = &handler_args.server_parameters
						 .https_cert_filename},
			{.env_var = "HTTP_TLS_KEY_PASSWORD",
			 .dst = &handler_args.server_parameters
						 .https_key_password},

	};

	for (size_t i = 0; i < RD_ARRAYSIZE(envs_config); ++i) {
		*envs_config[i].dst = getenv(envs_config[i].env_var);
	}

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
			"s?s"  /* https_cert_filename */
			"s?s"  /* https_key_filename */
			"s?s"  /* https_key_password */
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
			&handler_args.server_parameters.per_ip_connection_limit,
			"https_key_filename",
			&handler_args.server_parameters.https_key_filename,
			"https_key_password",
			&handler_args.server_parameters.https_key_password,
			"https_cert_filename",
			&handler_args.server_parameters.https_cert_filename);

	if (unpack_rc != 0 /* Failure */) {
		rdlog(LOG_ERR, "Can't parse HTTP options: %s", error.text);
		goto err;
	}

	if (handler_args.server_parameters.https_key_password &&
	    handler_args.server_parameters.https_key_password[0] == '@') {
		// Key password is in a file
		key_password = rd_file_read(
				&handler_args.server_parameters
						 .https_key_password[1],
				&key_password_len);
		handler_args.server_parameters.https_key_password =
				key_password;
	}

	struct http_listener *http_listener = start_http_loop(
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
