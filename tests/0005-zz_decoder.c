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

#include "zz_http2k_tests.h"

#include "src/decoder/zz_http2k/zz_http2k_parser.c"

#include "n2k_kafka_tests.h"

static const char *VALID_URL[] = {
		"/v1/topic1",
		"/v1/topic1/blabla",
		"/v1/topic1;blabla",
		"/v1/topic1/blabla",
		"/v1/topic1?blabla",
		"/v1/topic1:blabla",
		"/v1/topic1@blabla",
		"/v1/topic1=blabla",
		"/v1/topic1&blabla",
};

static const char *INVALID_URL[] = {
		"", "/", "/noversion", "/v2/topic", "/v1/", "?v1/topic",
};

static void check_zz_decoder_double0(rd_kafka_message_t *rkm[],
				     void *unused __attribute__((unused)),
				     size_t msgs_size) {
	size_t i = 0;
	json_error_t jerr;
	const char *client_mac, *application_name, *sensor_uuid;
	json_int_t a;

	for (i = 0; i < msgs_size; ++i) {
		json_t *root = json_loadb(
				rkm[i]->payload, rkm[i]->len, 0, &jerr);
		if (NULL == root) {
			rdlog(LOG_ERR, "Couldn't load file: %s", jerr.text);
			assert_true(0);
		}

		const int rc = json_unpack_ex(root,
					      &jerr,
					      0,
					      "{s:s,s:s,s:s,s:I}",
					      "client_mac",
					      &client_mac,
					      "application_name",
					      &application_name,
					      "sensor_uuid",
					      &sensor_uuid,
					      "a",
					      &a);

		if (rc != 0) {
			rdlog(LOG_ERR, "Couldn't unpack values: %s", jerr.text);
			assert_true(0);
		}

		if (i == 0) {
			assert_true(0 ==
				    strcmp(client_mac, "54:26:96:db:88:01"));
		} else {
			assert_true(0 ==
				    strcmp(client_mac, "54:26:96:db:88:02"));
		}
		assert_true(0 == strcmp(application_name, "wwww"));
		assert_true(0 == strcmp(sensor_uuid, "abc"));
		assert_true(a == 5);

		json_decref(root);
	}
}

static void check_zz_decoder_simple(rd_kafka_message_t *rkm[],
				    size_t rkm_len,
				    void *opaque) {
	assert_int_equal(rkm_len, 1);
	check_zz_decoder_double0(rkm, opaque, 1);
}

static void check_zz_decoder_double(rd_kafka_message_t *rkm[],
				    size_t rkm_len,
				    void *opaque) {
	assert_int_equal(rkm_len, 2);
	check_zz_decoder_double0(rkm, opaque, 2);
}

static void check_zz_decoder_simple_def(rd_kafka_message_t *rkm[],
					size_t msgs_num,
					void *unused __attribute__((unused))) {
	json_error_t jerr;
	const char *client_mac, *application_name, *sensor_uuid;
	int u;

	assert_int_equal(1, msgs_num);

	json_t *root = json_loadb(rkm[0]->payload, rkm[0]->len, 0, &jerr);
	if (NULL == root) {
		rdlog(LOG_ERR, "Couldn't load file: %s", jerr.text);
		assert_true(0);
	}

	const int rc = json_unpack_ex(root,
				      &jerr,
				      0,
				      "{s:s,s:s,s:s,s:b}",
				      "client_mac",
				      &client_mac,
				      "application_name",
				      &application_name,
				      "sensor_uuid",
				      &sensor_uuid,
				      "u",
				      &u);

	if (rc != 0) {
		rdlog(LOG_ERR, "Couldn't unpack values: %s", jerr.text);
		assert_true(0);
	}

	assert_true(0 == strcmp(client_mac, "54:26:96:db:88:02"));
	assert_true(0 == strcmp(application_name, "wwww"));
	assert_true(0 == strcmp(sensor_uuid, "def"));
	assert_true(0 != u);

	json_decref(root);
}

static void check_zz_decoder_object(rd_kafka_message_t *rkm[],
				    size_t msgs_num,
				    void *unused __attribute__((unused))) {
	json_error_t jerr;
	const char *client_mac, *application_name, *sensor_uuid;
	json_int_t a, t1;

	assert_true(1 == msgs_num);
	json_t *root = json_loadb(rkm[0]->payload, rkm[0]->len, 0, &jerr);
	if (NULL == root) {
		rdlog(LOG_ERR, "Couldn't load file: %s", jerr.text);
		assert_true(0);
	}

	const int rc = json_unpack_ex(root,
				      &jerr,
				      0,
				      "{s:s,s:s,s:s,s:I,s:{s:I}}",
				      "client_mac",
				      &client_mac,
				      "application_name",
				      &application_name,
				      "sensor_uuid",
				      &sensor_uuid,
				      "a",
				      &a,
				      "object",
				      "t1",
				      &t1);

	if (rc != 0) {
		rdlog(LOG_ERR, "Couldn't unpack values: %s", jerr.text);
		assert_true(0);
	}

	assert_true(0 == strcmp(client_mac, "54:26:96:db:88:01"));
	assert_true(0 == strcmp(application_name, "wwww"));
	assert_true(0 == strcmp(sensor_uuid, "abc"));
	assert_true(a == 5);

	json_decref(root);
}

int main() {
	// clang-format off
	json_t *listener_cfg = assert_json_loads("{"
			  "\"proto\": \"http\","
			  "\"port\": 2057,"
			  "\"mode\": \"epoll\","
			  "\"num_threads\": 2,"
			  "\"decode_as\": \"zz_http2k\""
			"}");
	// clang-format on

	const struct CMUnitTest tests[] = {
			// clang-format off
		// Simple decoding
		zz_decoder_test("abc message",
			ZZ_TESTS_PARAMS(.consumer_uuid = "abc",
				.listener_conf = listener_cfg),
			MESSAGE_IN("{\"client_mac\": \"54:26:96:db:88:01\", "
			           "\"application_name\": \"wwww\", "
			           "\"sensor_uuid\":\"abc\", \"a\":5}",
			           check_zz_decoder_simple,
			           1),

			// Free & Check that session has been freed
			MESSAGE_IN(NULL, NO_MESSAGES_CHECK, 0),
		),

		// Another simple decoding
		zz_decoder_test("def messages",
			ZZ_TESTS_PARAMS(.consumer_uuid = "abc",
				.listener_conf = listener_cfg),
			MESSAGE_IN("{\"client_mac\": \"54:26:96:db:88:02\", "
			           "\"application_name\": \"wwww\", "
			           "\"sensor_uuid\":\"def\", \"a\":5, "
			           "\"u\":true}",
			           check_zz_decoder_simple_def,
			           1),

			// Free & Check that session has been freed
			MESSAGE_IN(NULL, NO_MESSAGES_CHECK, 0),
		),

		// Two messages in the same received HTTP chunk
		zz_decoder_test("two messages",
			ZZ_TESTS_PARAMS(.consumer_uuid = "abc",
				.listener_conf = listener_cfg),
			MESSAGE_IN("{\"client_mac\": \"54:26:96:db:88:01\", "
			           "\"application_name\": \"wwww\", "
			           "\"sensor_uuid\":\"abc\", \"a\":5}"
			           "{\"client_mac\": \"54:26:96:db:88:02\", "
			           "\"application_name\": \"wwww\", "
			           "\"sensor_uuid\":\"abc\", \"a\":5}",
		                   check_zz_decoder_double,
		                   2),

			// Free & Check that session has been freed
			MESSAGE_IN(NULL, NO_MESSAGES_CHECK, 0),
		),

		// One message divided in two chunks
		zz_decoder_test("two chunks message",
			ZZ_TESTS_PARAMS(.consumer_uuid = "abc",
				.listener_conf = listener_cfg),
			MESSAGE_IN("{\"client_mac\": \"54:26:96:db:88:01\", ",
			           check_zero_messages, 0),
			MESSAGE_IN("\"application_name\": \"wwww\", "
			           "\"sensor_uuid\":\"abc\", \"a\":5}",
			           check_zz_decoder_simple,
			           1),
			// Free & Check that session has been freed
			MESSAGE_IN(NULL, NO_MESSAGES_CHECK, 0),
		),

		// One message divided in three chunks, string value divided too
		zz_decoder_test("three chunks message",
			ZZ_TESTS_PARAMS(.consumer_uuid = "abc",
				.listener_conf = listener_cfg),
			MESSAGE_IN("{\"client_mac\": \"54:26:96:",
			           check_zero_messages,
			           0),
			MESSAGE_IN("db:88:01\", \"application_name\": \"wwww\","
			           "\"sensor_uuid\":\"abc\", \"a\":5}",
			           check_zz_decoder_simple,
			           1),
			MESSAGE_IN("{\"client_mac\": \"", check_zero_messages,
				   0),
			MESSAGE_IN("54:26:96:db:88:01\", "
			           "\"application_name\": \"wwww\", "
			           "\"sensor_uuid\":\"abc\", \"a\":5}",
			           check_zz_decoder_simple,
			           1),
			// Free & Check that session has been freed
			MESSAGE_IN(NULL, NO_MESSAGES_CHECK, 0),
		),

		// JSON key divided into chunks
		zz_decoder_test("split key message",
			ZZ_TESTS_PARAMS(.consumer_uuid = "abc",
				.listener_conf = listener_cfg),
			MESSAGE_IN("{\"client_", check_zero_messages, 0),
			MESSAGE_IN("mac\": \"54:26:96:db:88:01\", "
				   "\"application_name\": \"wwww\", "
				   "\"sensor_uuid\":\"abc\", \"a\":5}",
				   check_zz_decoder_simple,
				   1),
			MESSAGE_IN("{\"client_mac", check_zero_messages, 0),
			MESSAGE_IN("\": \"54:26:96:db:88:01\", "
				   "\"application_name\": \"wwww\", "
				   "\"sensor_uuid\":\"abc\", \"a\":5}",
				   check_zz_decoder_simple,
				   1),
			/* Free & Check that session has been freed */
			MESSAGE_IN(NULL, NO_MESSAGES_CHECK, 0),
		),

		zz_decoder_test("message with object",
			ZZ_TESTS_PARAMS(.consumer_uuid = "abc",
				.listener_conf = listener_cfg),
			MESSAGE_IN("{\"client_", check_zero_messages, 0),
			MESSAGE_IN("mac\": \"54:26:96:db:88:01\", "
			           "\"application_name\": \"wwww\", "
			           "\"sensor_uuid\":\"abc\", "
			           "\"object\":{\"t1\":1}, "
			           "\"a\":5}",
			           check_zz_decoder_object,
			           1),
			/* Free & Check that session has been freed */
			MESSAGE_IN(NULL, NO_MESSAGES_CHECK, 0),
		),

		zz_decoder_test("No consumer id",
			// no consumer id -> input topic == output_topic
			ZZ_TESTS_PARAMS(.listener_conf = listener_cfg),
			MESSAGE_IN("{\"client_mac\": \"54:26:96:db:88:01\", "
			           "\"application_name\": \"wwww\", "
			           "\"sensor_uuid\":\"abc\", \"a\":5}",
			           check_zz_decoder_simple,
			           1),

			// Free & Check that session has been freed
			MESSAGE_IN(NULL, NO_MESSAGES_CHECK, 0),
		),
			// clang-format on
	};

	const int cmocka_run_rc = run_zz_decoder_group_tests(tests);
	json_decref(listener_cfg);
	return cmocka_run_rc;
}
