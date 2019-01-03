/*
**
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
** Copyright (C) 2018-2019, Wizzie S.L.
** Author: Eugenio Perez <eupm90@gmail.com>
**
** This file is part of n2kafka.
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

#include "meraki.h"
#include "decoder/zz_http2k/zz_http2k_decoder.h"

#include "util/kafka.h"
#include "util/pair.h"

#include <jansson.h>
#include <librd/rd.h>
#include <librd/rdlog.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

static const char CONFIG_MERAKI_DECODER_NAME[] = "meraki";
static const char CONFIG_MERAKI_TOPIC_KEY[] = "topic";

/*
    ENRICHMENT
*/

struct meraki_listener_opaque {
#ifndef NDEBUG
#define MERAKI_LISTENER_OPAQUE 0x3A10AEA1C
	uint64_t magic;
#endif

	char listener_topic[1];
};

#define meraki_listener_opaque_cast(listener_opaque)                           \
	({                                                                     \
		struct meraki_listener_opaque *meraki_listener_opaque_cast =   \
				listener_opaque;                               \
		(void)meraki_listener_opaque_cast;                             \
		assert((meraki_listener_opaque_cast)->magic ==                 \
		       MERAKI_LISTENER_OPAQUE);                                \
		listener_opaque;                                               \
	})

static int meraki_opaque_creator(const struct json_t *config, void **_opaque) {
	assert(config);

	const char *listener_topic_name = NULL;
	size_t listener_topic_name_len = 0;

	const json_t *jlistener_topic_name =
			json_object_get(config, CONFIG_MERAKI_TOPIC_KEY);

	if (!jlistener_topic_name) {
		return 0; // no need for listener opaque
	}

	if (!json_is_string(jlistener_topic_name)) {
		rdlog(LOG_ERR,
		      "meraki listener-configured topic is not a "
		      "string in config");
	} else {
		listener_topic_name = json_string_value(jlistener_topic_name);
		listener_topic_name_len =
				json_string_length(jlistener_topic_name);
	}

	struct meraki_listener_opaque *opaque = (*_opaque) = calloc(
			1, sizeof(*opaque) + listener_topic_name_len + 1);
	if (NULL == opaque) {
		rdlog(LOG_ERR,
		      "%s",
		      "Can't allocate meraki opaque (out of memory?)");
		return -1;
	}

#ifdef MERAKI_LISTENER_OPAQUE
	opaque->magic = MERAKI_LISTENER_OPAQUE;
#endif

	memcpy(opaque->listener_topic,
	       listener_topic_name,
	       listener_topic_name_len);

	return 0;
}

static void meraki_opaque_destructor(void *vopaque) {
	if (vopaque) {
		struct meraki_listener_opaque *opaque =
				meraki_listener_opaque_cast(vopaque);
		free(opaque);
	}
}

static int new_meraki_session(void *zz_sess,
			      struct meraki_listener_opaque *listener_opaque,
			      const keyval_list_t *msg_vars) {
	assert(zz_sess);
	assert(msg_vars);

	static const char meraki_topic_prefix[] = "/v1/data/";
	const char *meraki_topic = "";

	if (listener_opaque) {
		assert(listener_opaque->listener_topic);
		meraki_topic = listener_opaque->listener_topic;
	} else if (default_topic_name()) {
		meraki_topic = default_topic_name();
	}

	char meraki_uri[strlen(meraki_topic_prefix) + strlen(meraki_topic) + 1];
	memcpy(meraki_uri, meraki_topic_prefix, strlen(meraki_topic_prefix));
	memcpy(meraki_uri + strlen(meraki_topic_prefix),
	       meraki_topic,
	       strlen(meraki_topic) + 1);

	struct pair zz_keyvals0[] = {
			{.key = "D-HTTP-URI", .value = meraki_uri},
			{.key = "D-Client-IP",
			 .value = valueof(msg_vars, "D-Client-IP")},
	};

	keyval_list_t zz_keyvals = keyval_list_initializer(zz_keyvals);
	for (size_t i = 0; i < RD_ARRAYSIZE(zz_keyvals0); ++i) {
		add_key_value_pair(&zz_keyvals, &zz_keyvals0[i]);
	}

	return zz_decoder.new_session(zz_sess, NULL, &zz_keyvals);
}

static int vnew_meraki_session(void *zz_session,
			       void *vlistener_opaque,
			       const keyval_list_t *msg_vars) {
	struct meraki_listener_opaque *listener_opaque =
			vlistener_opaque ? meraki_listener_opaque_cast(
							   vlistener_opaque)
					 : vlistener_opaque;

	return new_meraki_session(zz_session, listener_opaque, msg_vars);
}

static const char *url_validator(const char *url) {
	static const char meraki_prefix[] = "/v1/meraki";
	return strncmp(meraki_prefix, url, strlen(meraki_prefix))
			       ? NULL
			       : url + sizeof(meraki_prefix);
}

static enum decoder_callback_err meraki_decode(const char *buffer,
					       size_t buf_size,
					       const keyval_list_t *attrs,
					       void *_listener_callback_opaque,
					       const char **response,
					       size_t *response_size,
					       void *sessionp) {
	const char *http_method = valueof(attrs, "D-HTTP-method");

	if (0 == strcmp(http_method, "GET")) {
		const char *uri = valueof(attrs, "D-HTTP-URI");
		*response = url_validator(uri);
		if (NULL == *response) {
			rdlog(LOG_ERR, "Invalid URI %s", uri);
			return DECODER_CALLBACK_RESOURCE_NOT_FOUND;
		}

		*response_size = strlen(*response);
		return DECODER_CALLBACK_OK;
	} else {
		return zz_decoder.callback(buffer,
					   buf_size,
					   attrs,
					   _listener_callback_opaque,
					   response,
					   response_size,
					   sessionp);
	}
}

static const char *meraki_decoder_name() {
	return CONFIG_MERAKI_DECODER_NAME;
}

static size_t meraki_session_size() {
	return zz_decoder.session_size();
}

static void free_meraki_session(void *t_session) {
	zz_decoder.delete_session(t_session);
}

const struct n2k_decoder meraki_decoder = {
		.name = meraki_decoder_name,

		.opaque_creator = meraki_opaque_creator,
		/* @TODO .opaque_reload = meraki_opaque_reload,*/
		.opaque_destructor = meraki_opaque_destructor,

		.new_session = vnew_meraki_session,
		.delete_session = free_meraki_session,
		.session_size = meraki_session_size,
		.callback = meraki_decode,
};
