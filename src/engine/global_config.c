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

#include "config.h" // for HAVE_LIBMICROHTTPD

#include "global_config.h"

#include "decoder/decoder_api.h"
#include "decoder/dumb/dumb.h"
#include "decoder/meraki/meraki.h"
#include "decoder/zz_http2k/zz_http2k_decoder.h"
#ifdef HAVE_LIBMICROHTTPD
#include "listener/http/http.h"
#endif
#include "listener/listener_api.h"
#include "listener/socket/socket.h"

#include "util/kafka.h"
#include "util/util.h"

#include <jansson.h>
#include <librd/rd.h>
#include <librd/rdfile.h>
#include <librd/rdlog.h>
#include <librd/rdsysqueue.h>
#include <librdkafka/rdkafka.h>

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <syslog.h>

#ifndef LIST_FOREACH_SAFE
#define LIST_FOREACH_SAFE(var, head, field, tvar)                              \
	for ((var) = LIST_FIRST((head));                                       \
	     (var) && ((tvar) = LIST_NEXT((var), field), 1);                   \
	     (var) = (tvar))
#endif

#define CONFIG_LISTENERS_ARRAY "listeners"
#define CONFIG_TOPIC_KEY "topic"
#define CONFIG_BROKERS_KEY "brokers"
#define CONFIG_DEBUG_KEY "debug"
#define CONFIG_RESPONSE_KEY "response"
#define CONFIG_BLACKLIST_KEY "blacklist"
#define CONFIG_RDKAFKA_KEY "rdkafka."

struct n2kafka_config global_config;

static const struct n2k_decoder *registered_decoders[] = {
		&dumb_decoder, &meraki_decoder, &zz_decoder};

static const n2k_listener_factory *registered_listeners[] = {
#ifdef HAVE_LIBMICROHTTPD
		&http_listener_factory,
#endif
		&tcp_listener_factory,
		&udp_listener_factory,
};

void init_global_config() {
	memset(&global_config, 0, sizeof(global_config));
	global_config.kafka_conf = rd_kafka_conf_new();
	global_config.kafka_topic_conf = rd_kafka_topic_conf_new();
	global_config.blacklist = in_addr_list_new();
	global_config.log_severity = LOG_INFO;
	LIST_INIT(&global_config.listeners);

	size_t i;
	for (i = 0; i < RD_ARRAYSIZE(registered_decoders); ++i) {
		const n2k_decoder *decoder = registered_decoders[i];
		if (!decoder->init) {
			continue;
		}

		const int rc = decoder->init();
		if (0 != rc) {
			rdlog(LOG_ERR,
			      "Couldn't init %s decoder",
			      decoder->name());
		}
	}
}

static const char *assert_json_string(const char *key, const json_t *value) {
	if (!json_is_string(value)) {
		fatal("%s value must be a string in config file", key);
	}
	return json_string_value(value);
}

static void *assert_pton(int af, const char *src, void *dst) {
	const int pton_rc = inet_pton(af, src, dst);
	if (pton_rc < 0) {
		fatal("pton(%s) error: %s", src, strerror(errno));
	}
	return dst;
}

static void parse_response(const char *filename) {
	global_config.response =
			rd_file_read(filename, &global_config.response_len);
	if (global_config.response == NULL) {
		fatal("Cannot open response file %s: %s",
		      filename,
		      gnu_strerror_r(errno));
	}
}

static void parse_rdkafka_keyval_config(const char *key, const char *value) {
	rd_kafka_conf_res_t res;
	char errstr[512];

	const char *name = key + strlen(CONFIG_RDKAFKA_KEY);

	res = RD_KAFKA_CONF_UNKNOWN;
	/* Try "topic." prefixed properties on topic
	 * conf first, and then fall through to global if
	 * it didnt match a topic configuration property. */
	if (!strncmp(name, "topic.", strlen("topic.")))
		res = rd_kafka_topic_conf_set(global_config.kafka_topic_conf,
					      name + strlen("topic."),
					      value,
					      errstr,
					      sizeof(errstr));

	if (res == RD_KAFKA_CONF_UNKNOWN)
		res = rd_kafka_conf_set(global_config.kafka_conf,
					name,
					value,
					errstr,
					sizeof(errstr));

	if (res != RD_KAFKA_CONF_OK)
		fatal("%s", errstr);
}

static void parse_rdkafka_config_json(const char *key, const json_t *jvalue) {
	// Extracted from Magnus Edenhill's kafkacat
	const char *value = assert_json_string(key, jvalue);
	parse_rdkafka_keyval_config(key, value);
}

static void parse_blacklist(const json_t *config_array) {
	assert(json_is_array(config_array));

	size_t index;
	const json_t *value;
	json_array_foreach(config_array, index, value) {
		struct in_addr addr;
		const char *addr_string =
				assert_json_string("blacklist values", value);
		rdlog(LOG_INFO, "adding %s address to blacklist", addr_string);
		assert_pton(AF_INET, addr_string, &addr);
		in_addr_list_add(global_config.blacklist, &addr);
	}
}

static const n2k_listener_factory *listener_for(const char *proto) {
	size_t i;
	for (i = 0; i < RD_ARRAYSIZE(registered_listeners); ++i) {
		if (0 == strcmp(registered_listeners[i]->name(), proto)) {
			return registered_listeners[i];
		}
	}

	return NULL;
}

static const struct n2k_decoder *
locate_registered_decoder(const char *decode_as) {
	assert(decode_as);

	size_t i;
	for (i = 0; i < RD_ARRAYSIZE(registered_decoders); ++i) {
		assert(NULL != registered_decoders[i]->name());

		if (0 == strcmp(registered_decoders[i]->name(), decode_as)) {
			return registered_decoders[i];
		}
	}

	return NULL;
}

static void parse_listener(json_t *config) {
	const char *proto = NULL, *decode_as = "";
	json_error_t json_err;

	const int unpack_rc = json_unpack_ex(config,
					     &json_err,
					     0,
					     "{s:s,s?s}",
					     "proto",
					     &proto,
					     "decode_as",
					     &decode_as);

	if (unpack_rc != 0) {
		rdlog(LOG_ERR, "Can't parse listener: %s", json_err.text);
		return;
	}

	if (NULL == proto) {
		rdlog(LOG_ERR, "Can't create a listener with no proto");
		return;
	}

	const n2k_listener_factory *listener_factory = listener_for(proto);
	if (NULL == listener_factory) {
		rdlog(LOG_ERR,
		      "Can't find listener creator for protocol %s",
		      proto);
		return;
	}

	assert(decode_as);
	const struct n2k_decoder *decoder =
			locate_registered_decoder(decode_as);
	if (NULL == decoder) {
		rdlog(LOG_ERR, "Can't locate decoder type %s", decode_as);
		exit(-1);
	}

	struct listener *proto_listener =
			listener_factory->create(config, decoder);

	if (NULL == proto_listener) {
		rdlog(LOG_ERR, "Can't create listener for proto %s.", proto);
		exit(-1);
	}

	LIST_INSERT_HEAD(&global_config.listeners, proto_listener, entry);
}

static void parse_rdkafka_config_keyval(const char *key, const json_t *value) {
	if (!strcasecmp(key, CONFIG_TOPIC_KEY)) {
		global_config.topic = strdup(assert_json_string(key, value));
	} else if (!strcasecmp(key, CONFIG_BROKERS_KEY)) {
		const char *brokers = assert_json_string(key, value);
		parse_rdkafka_keyval_config("rdkafka.metadata.broker.list",
					    brokers);
	} else if (!strncasecmp(key,
				CONFIG_RDKAFKA_KEY,
				strlen(CONFIG_RDKAFKA_KEY))) {
		// if starts with
		parse_rdkafka_config_json(key, value);
	}
}

void parse_config(const char *config_file_path) {
	const char *key;
	json_error_t jerror;
	json_t *json_val, *json_tmp;
	global_config.config_path = config_file_path;
	json_t *root = json_load_file(config_file_path, 0, &jerror);
	if (root == NULL) {
		rblog(LOG_ERR,
		      "Error parsing config file %s: %s, line %d: %s",
		      jerror.source,
		      jerror.text,
		      jerror.line,
		      jerror.text);
		exit(1);
	}

	// rdkafka configuration
	json_t *rdkafka_configs = json_array();
	if (NULL == rdkafka_configs) {
		rdlog(LOG_ERR, "Can't allocate a json array (OOM?)");
		goto kafka_array_err;
	}

	// Need to parse kafka stuff, and delete it from the config
	json_object_foreach_safe(root, json_tmp, key, json_val) {
		parse_rdkafka_config_keyval(key, json_val);
	}

	const char *response_file = NULL;
	json_t *blacklist = NULL, *listeners = NULL;

	// Parse global stuff
	const int json_unpack_rc = json_unpack_ex(root,
						  &jerror,
						  0,
						  "{s?i,s?s,s?o,s:o}",
						  CONFIG_DEBUG_KEY,
						  &global_config.log_severity,
						  CONFIG_RESPONSE_KEY,
						  &response_file,
						  CONFIG_BLACKLIST_KEY,
						  &blacklist,
						  CONFIG_LISTENERS_ARRAY,
						  &listeners);

	if (json_unpack_rc != 0) {
		rdlog(LOG_ERR, "Can't unpack config json: %s", jerror.text);
	}

	rd_log_set_severity(global_config.log_severity);

	if (response_file) {
		parse_response(response_file);
	}

	if (blacklist) {
		parse_blacklist(blacklist);
	}

	init_rdkafka();

	if (rdkafka_configs) {
		json_decref(rdkafka_configs);
		rdkafka_configs = NULL;
	}

	size_t index;
	json_array_foreach(listeners, index, json_val) {
		parse_listener(json_val);
	}

kafka_array_err:
	json_decref(root);
}

static void shutdown_listener(struct listener *i) {
	rblog(LOG_INFO, "Joining listener on port %d.", i->port);
	if (NULL == i->join) {
		return;
	}

	i->join(i);
}

static void shutdown_listeners(struct n2kafka_config *config) {
	struct listener *i = NULL, *aux = NULL;
	LIST_FOREACH_SAFE(i, &config->listeners, entry, aux)
	shutdown_listener(i);
}

/// Close listener that are not valid anymore, and reload present ones
static void
reload_listeners_check_already_present(json_t *new_listeners,
				       struct n2kafka_config *config) {
	json_error_t jerr;
	size_t _index = 0;
	struct listener *i = NULL, *aux = NULL;
	LIST_FOREACH_SAFE(i, &config->listeners, entry, aux) {
		const uint16_t i_port = i->port;

		json_t *found_value = NULL;
		json_t *value = NULL;

		json_array_foreach(new_listeners, _index, value) {
			int port = 0;

			if (NULL != found_value)
				break;

			json_unpack_ex(value, &jerr, 0, "{s:i}", "port", &port);

			if (port > 0 && port == i_port)
				found_value = value;
		}

		if (found_value && i->reload) {
			rdlog(LOG_INFO,
			      "Reloading listener on port %d",
			      i_port);
			i->reload(i, found_value);
		} else {
			LIST_REMOVE(i, entry);
			shutdown_listener(i);
		}
	}
}

/// Creating new declared listeners
static void reload_listeners_create_new_ones(json_t *new_listeners_array,
					     struct n2kafka_config *config) {
	json_error_t jerr;
	size_t _index = 0;
	json_t *new_config_listener = 0;
	struct listener *i = NULL;
	json_array_foreach(new_listeners_array, _index, new_config_listener) {
		uint16_t searched_port = 0;

		struct listener *found_value = NULL;
		i = NULL;

		json_unpack_ex(new_config_listener,
			       &jerr,
			       0,
			       "{s:i}",
			       "port",
			       &searched_port);

		LIST_FOREACH(i, &config->listeners, entry) {
			uint16_t port = i->port;

			if (NULL != found_value)
				break;

			if (port > 0 && port == searched_port)
				found_value = i;
		}

		if (NULL == found_value) {
			// new listener, need to create
			parse_listener(new_config_listener);
		}
	}
}

static void
reload_listeners(json_t *new_config, struct n2kafka_config *config) {
	json_error_t jerr;

	json_t *listeners_array = NULL;
	const int json_unpack_rc = json_unpack_ex(new_config,
						  &jerr,
						  0,
						  "{s:o}",
						  "listeners",
						  &listeners_array);

	if (json_unpack_rc != 0) {
		rdlog(LOG_ERR, "Can't extract listeners array: %s", jerr.text);
		return;
	}

	reload_listeners_check_already_present(listeners_array, config);
	reload_listeners_create_new_ones(listeners_array, config);
}

typedef int (*reload_cb)(void *database, const struct json_t *config);

void reload_config(struct n2kafka_config *config) {
	json_error_t jerr;

	assert(config);
	if (config->config_path == NULL) {
		rblog(LOG_ERR, "Have no config file to reload");
		return;
	}

	json_t *new_config_file = json_load_file(config->config_path, 0, &jerr);
	if (NULL == new_config_file) {
		rdlog(LOG_ERR,
		      "Can't parse new config file: %s at line %d, column %d",
		      jerr.text,
		      jerr.line,
		      jerr.column);
	}

	reload_listeners(new_config_file, config);
	json_decref(new_config_file);
}

void free_global_config() {
	size_t i;
	shutdown_listeners(&global_config);

	for (i = 0; i < RD_ARRAYSIZE(registered_decoders); ++i) {
		if (registered_decoders[i]->done) {
			registered_decoders[i]->done();
		}
	}

	flush_kafka();
	stop_rdkafka();

	in_addr_list_done(global_config.blacklist);
	free(global_config.topic);
	free(global_config.response);
}
