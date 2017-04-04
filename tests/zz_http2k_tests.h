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

#include "src/decoder/zz_http2k/zz_http2k_decoder.h"

#include <setjmp.h>

#include <cmocka.h>
#include <jansson.h>
#include <microhttpd.h>

#include <netinet/in.h>

static const char zz_topic_template[] = "n2ktXXXXXX";
static const char zz_uri_prefix[] = "/v1/";

/// Prints uri based on uuid and topic
#define print_expected_topic(dst, dst_size, uuid, topic)                       \
	snprintf(dst, dst_size, "%s_%s", uuid, topic)

#define print_expected_url(dst, dst_size, uuid, topic)                         \
	snprintf(dst, dst_size, "/v1/%s", topic)

/// Prepare decoder args
void prepare_args(const char *uri,
		  const char *consumer_uuid,
		  const char *client_ip,
		  struct pair *mem,
		  size_t memsiz,
		  keyval_list_t *list);

struct message_in {
	const char *msg;
	size_t size;
};

typedef void (*check_callback_fn)(const rd_kafka_message_t *[],
				  size_t,
				  void *opaque);

/// zz_http2k test parameters
struct zz_http2k_params {
	const char *uri;	   ///< POST request URI
	const char *consumer_uuid; ///< Detected consumer uuid
	const char *topic;	 ///< Topic to expect messages
};

/** Template for zz_decoder test
	@param listener_conf Listener config
	@param decoder_conf Decoder config
	@param params test parameters
	@param msgs Input messages
	@param msgs_len Length of msgs
	@param check_callback Array of functions that will be called with each
	session status. It is suppose to be the same length as msgs array.
	@param check_callback_opaque Opaque used in the second parameter of
	check_callback[iteration] call
	*/
void test_zz_decoder0(const json_t *listener_conf,
		      const json_t *decoder_conf,
		      const struct zz_http2k_params *params,
		      const struct message_in *msgs,
		      const check_callback_fn *check_callback,
		      size_t msgs_len,
		      const size_t *expected_kafka_msgs,
		      void *check_callback_opaque);

/** Function that check that no messages has been sent
	@param msgs Messages received
	@param msgs_len Length of msgs
	@param unused context information
*/
static void __attribute__((unused))
check_zero_messages(const rd_kafka_message_t *msgs[],
		    size_t msgs_len,
		    void *unused __attribute__((unused))) {

	assert_non_null(msgs);
	assert_true(0 == msgs_len);
}

#define NO_MESSAGES_CHECK NULL

json_t *assert_json_loads(const char *json_txt);

/// @TODO make private!
struct mock_MHD_connection_value {
	enum MHD_ValueKind kind;
	const char *key, *value;
};

/// @TODO make private!
struct mock_MHD_connection {
#define MOCK_MHD_CONNECTION_MAGIC 0x0CDC0310A1CCDC031
	uint64_t magic;
	union MHD_ConnectionInfo ret_client_addr;
	struct sockaddr_in client_addr;

	struct {
		size_t size;
		const struct mock_MHD_connection_value *values;
	} connection_values;
};

/// @TODO make private!
struct zz_test_state {
	struct zz_config zz_config;
	struct mock_MHD_connection mhd_connection;
	struct listener *listener;
};

/// @TODO make private!
void test_zz_decoder_setup(struct zz_test_state *state,
			   const json_t *decoder_conf);

/// @TODO make private!
void test_zz_decoder_teardown(void *vstate);
