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
#include <librd/rd.h>
#include <librdkafka/rdkafka.h>
#include <microhttpd.h>

#include <netinet/in.h>

/// Prepare decoder args
void prepare_args(const char *uri,
		  const char *consumer_uuid,
		  const char *client_ip,
		  struct pair *mem,
		  size_t memsiz,
		  keyval_list_t *list);

struct message_in {
	const char *msg;		///< Input message
	size_t size;			///< Size of the message
	size_t expected_kafka_messages; ///< Expected produced messages

	/// Callback to check msgs (1) of msgs_size (2) with a given opaque (3)
	void (*check_callback_fn)(rd_kafka_message_t *[], size_t, void *);
	void *check_cb_opaque; ///< check_callback_fn opaque
};

/// Helper macro to create message_in struct
#define MESSAGE_IN(t_msg, t_check_callback_fn, t_expected_kafka_messages)      \
	{                                                                      \
		.msg = t_msg, .size = sizeof(t_msg) - 1,                       \
		.expected_kafka_messages = t_expected_kafka_messages,          \
		.check_callback_fn = t_check_callback_fn,                      \
	}

/// zz_http2k test parameters
struct zz_http2k_params {
	const json_t *listener_conf; ///< Listener config
	const json_t *decoder_conf;  ///< Decoder config

	const char *uri;	   ///< POST request URI
	const char *consumer_uuid; ///< Detected consumer uuid
	const char *topic;	 ///< Topic to expect messages
	const char *out_topic;     ///< Topic to expect messages

	/// Test messages
	struct {
		const struct message_in *msg; ///< Actual messages
		size_t num;		      ///< msg length
	} msgs;
};

#define ARGS(...) __VA_ARGS__
/// Adapt test messages to use with
#define ZZ_TESTS_PARAMS(...) ARGS(__VA_ARGS__)

/// Sanitize pessages
#define ZZ_TESTS_MSGS(...)                                                     \
	(struct message_in[]) {                                                \
		ARGS(__VA_ARGS__)                                              \
	}

// clang-format off
/** http2k decoder test
  @param name Test name
  @param t_params Test parameters (use with ZZ_TEST_PARAMS)
  @param ... Test messages (using MESSAGE_IN for each message)
 */
#define zz_decoder_test(t_name, t_params, ...) {                                                                      \
	.name = t_name, .test_func = test_zz_decoder,                          \
	.initial_state = &(struct zz_http2k_params) {                          \
		.msgs = {                                                      \
			.msg = ZZ_TESTS_MSGS(__VA_ARGS__),                     \
			.num = RD_ARRAY_SIZE(ZZ_TESTS_MSGS(__VA_ARGS__))       \
		},                                                             \
		t_params                                                       \
	}}                                                                     \
// clang-format on

void test_zz_decoder(void **test_params);

/** Function that check that no messages has been sent
	@param msgs Messages received
	@param msgs_len Length of msgs
	@param unused context information
*/
static void __attribute__((unused))
check_zero_messages(rd_kafka_message_t *msgs[],
		    size_t msgs_len,
		    void *unused __attribute__((unused))) {

	assert_non_null(msgs);
	assert_true(0 == msgs_len);
}

#define NO_MESSAGES_CHECK NULL

json_t *assert_json_loads(const char *json_txt);

int test_zz_decoder_group_tests_setup(void **state);
int test_zz_decoder_group_tests_teardown(void **state);

#define run_zz_decoder_group_tests(t_tests)                                    \
	cmocka_run_group_tests(t_tests,                                        \
			       test_zz_decoder_group_tests_setup,              \
			       test_zz_decoder_group_tests_teardown);
