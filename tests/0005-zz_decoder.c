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

#include <setjmp.h>

#include <cmocka.h>

static json_t *listener_cfg = NULL;
static json_t *decoder_cfg = NULL;

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

static void check_zz_decoder_double0(const rd_kafka_message_t *rkm[],
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

static void check_zz_decoder_simple(const rd_kafka_message_t *rkm[],
				    size_t rkm_len,
				    void *opaque) {
	assert_int_equal(rkm_len, 1);
	check_zz_decoder_double0(rkm, opaque, 1);
}

static void check_zz_decoder_double(const rd_kafka_message_t *rkm[],
				    size_t rkm_len,
				    void *opaque) {
	assert_int_equal(rkm_len, 2);
	check_zz_decoder_double0(rkm, opaque, 2);
}

static void check_zz_decoder_simple_def(const rd_kafka_message_t *rkm[],
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

static void check_zz_decoder_object(const rd_kafka_message_t *rkm[],
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

static void test_zz_decoder_simple() {
	/// @TODO join with all other tests!
	static const char consumer_uuid[] = "abc";
	char topic[sizeof(zz_topic_template)];
	strcpy(topic, zz_topic_template);
	random_topic_name(topic);

	const size_t out_topic_len = (size_t)print_expected_topic(
			NULL, 0, consumer_uuid, topic);
	char out_topic[out_topic_len];
	print_expected_topic(out_topic, out_topic_len, consumer_uuid, topic);

	const size_t uri_len = (size_t)print_expected_url(
			NULL, 0, consumer_uuid, topic);
	char uri[uri_len];
	print_expected_url(uri, uri_len, consumer_uuid, topic);

#define MESSAGES                                                               \
	X("{\"client_mac\": \"54:26:96:db:88:01\", "                           \
	  "\"application_name\": \"wwww\", \"sensor_uuid\":\"abc\", \"a\":5}", \
	  check_zz_decoder_simple,                                             \
	  1)                                                                   \
	/* Free & Check that session has been freed */                         \
	X(NULL, NO_MESSAGES_CHECK, 0)

	struct message_in msgs[] = {
#define X(a, fn, kafka_msgs) {a, sizeof(a) - 1},
			MESSAGES
#undef X
	};

	static const check_callback_fn callbacks_functions[] = {
#define X(a, fn, kafka_msgs) fn,
			MESSAGES
#undef X
	};

	static const size_t expected_kafka_msgs[] = {
#define X(a, fn, kafka_msgs) kafka_msgs,
			MESSAGES
#undef X
	};

	test_zz_decoder0(listener_cfg,
			 NULL,
			 &(struct zz_http2k_params){
					 .uri = uri,
					 .consumer_uuid = consumer_uuid,
					 .topic = out_topic,
			 },
			 msgs,
			 callbacks_functions,
			 RD_ARRAYSIZE(msgs),
			 expected_kafka_msgs,
			 NULL);

#undef MESSAGES
}

/// Simple decoding with another enrichment
static void test_zz_decoder_simple_def() {
	/// @TODO join with all other tests!
	static const char consumer_uuid[] = "abc";
	char topic[sizeof(zz_topic_template)];
	strcpy(topic, zz_topic_template);
	random_topic_name(topic);

	const size_t out_topic_len = (size_t)print_expected_topic(
			NULL, 0, consumer_uuid, topic);
	char out_topic[out_topic_len];
	print_expected_topic(out_topic, out_topic_len, consumer_uuid, topic);

	const size_t uri_len = (size_t)print_expected_url(
			NULL, 0, consumer_uuid, topic);
	char uri[uri_len];
	print_expected_url(uri, uri_len, consumer_uuid, topic);

#define MESSAGES                                                               \
	X("{\"client_mac\": \"54:26:96:db:88:02\", "                           \
	  "\"application_name\": \"wwww\", \"sensor_uuid\":\"def\", "          \
	  "\"a\":5, \"u\":true}",                                              \
	  check_zz_decoder_simple_def,                                         \
	  1)                                                                   \
	/* Free & Check that session has been freed */                         \
	X(NULL, NO_MESSAGES_CHECK, 0)

	struct message_in msgs[] = {
#define X(a, fn, kafka_msgs) {a, sizeof(a) - 1},
			MESSAGES
#undef X
	};

	check_callback_fn callbacks_functions[] = {
#define X(a, fn, kafka_msgs) fn,
			MESSAGES
#undef X
	};

	static const size_t expected_kafka_msgs[] = {
#define X(a, fn, kafka_msgs) kafka_msgs,
			MESSAGES
#undef X
	};

	test_zz_decoder0(listener_cfg,
			 decoder_cfg,
			 &(struct zz_http2k_params){
					 .uri = uri,
					 .consumer_uuid = consumer_uuid,
					 .topic = out_topic,
			 },
			 msgs,
			 callbacks_functions,
			 RD_ARRAYSIZE(msgs),
			 expected_kafka_msgs,
			 NULL);

#undef MESSAGES
}

/** Two messages in the same input string */
static void test_zz_decoder_double() {
	/// @TODO join with all other tests!
	static const char consumer_uuid[] = "abc";
	char topic[sizeof(zz_topic_template)];
	strcpy(topic, zz_topic_template);
	random_topic_name(topic);

	const size_t out_topic_len = (size_t)print_expected_topic(
			NULL, 0, consumer_uuid, topic);
	char out_topic[out_topic_len];
	print_expected_topic(out_topic, out_topic_len, consumer_uuid, topic);

	const size_t uri_len = (size_t)print_expected_url(
			NULL, 0, consumer_uuid, topic);
	char uri[uri_len];
	print_expected_url(uri, uri_len, consumer_uuid, topic);

#define MESSAGES                                                               \
	X("{\"client_mac\": \"54:26:96:db:88:01\", "                           \
	  "\"application_name\": \"wwww\", \"sensor_uuid\":\"abc\", \"a\":5}"  \
	  "{\"client_mac\": \"54:26:96:db:88:02\", "                           \
	  "\"application_name\": \"wwww\", \"sensor_uuid\":\"abc\", \"a\":5}", \
	  check_zz_decoder_double,                                             \
	  2)                                                                   \
	/* Free & Check that session has been freed */                         \
	X(NULL, NO_MESSAGES_CHECK, 0)

	struct message_in msgs[] = {
#define X(a, fn, kafka_msgs) {a, sizeof(a) - 1},
			MESSAGES
#undef X
	};

	check_callback_fn callbacks_functions[] = {
#define X(a, fn, kafka_msgs) fn,
			MESSAGES
#undef X
	};

	static const size_t expected_kafka_msgs[] = {
#define X(a, fn, kafka_msgs) kafka_msgs,
			MESSAGES
#undef X
	};

	test_zz_decoder0(listener_cfg,
			 decoder_cfg,
			 &(struct zz_http2k_params){
					 .uri = uri,
					 .consumer_uuid = consumer_uuid,
					 .topic = out_topic,
			 },
			 msgs,
			 callbacks_functions,
			 RD_ARRAYSIZE(msgs),
			 expected_kafka_msgs,
			 NULL);

#undef MESSAGES
}

static void test_zz_decoder_half() {
	/// @TODO join with all other tests!
	static const char consumer_uuid[] = "abc";
	char topic[sizeof(zz_topic_template)];
	strcpy(topic, zz_topic_template);
	random_topic_name(topic);

	const size_t out_topic_len = (size_t)print_expected_topic(
			NULL, 0, consumer_uuid, topic);
	char out_topic[out_topic_len];
	print_expected_topic(out_topic, out_topic_len, consumer_uuid, topic);

	const size_t uri_len = (size_t)print_expected_url(
			NULL, 0, consumer_uuid, topic);
	char uri[uri_len];
	print_expected_url(uri, uri_len, consumer_uuid, topic);

#define MESSAGES                                                               \
	X("{\"client_mac\": \"54:26:96:db:88:01\", ", check_zero_messages, 0)  \
	X("\"application_name\": \"wwww\", \"sensor_uuid\":\"abc\", \"a\":5}", \
	  check_zz_decoder_simple,                                             \
	  1)                                                                   \
	/* Free & Check that session has been freed */                         \
	X(NULL, NO_MESSAGES_CHECK, 0)

	struct message_in msgs[] = {
#define X(a, fn, kafka_msgs) {a, sizeof(a) - 1},
			MESSAGES
#undef X
	};

	check_callback_fn callbacks_functions[] = {
#define X(a, fn, kafka_msgs) fn,
			MESSAGES
#undef X
	};

	static const size_t expected_kafka_msgs[] = {
#define X(a, fn, kafka_msgs) kafka_msgs,
			MESSAGES
#undef X
	};

	test_zz_decoder0(listener_cfg,
			 decoder_cfg,
			 &(struct zz_http2k_params){
					 .uri = uri,
					 .consumer_uuid = consumer_uuid,
					 .topic = out_topic,
			 },
			 msgs,
			 callbacks_functions,
			 RD_ARRAYSIZE(msgs),
			 expected_kafka_msgs,
			 NULL);

#undef MESSAGES
}

/** Checks that the decoder can handle to receive the half of a string */
static void test_zz_decoder_half_string() {
	/// @TODO join with all other tests!
	static const char consumer_uuid[] = "abc";
	char topic[sizeof(zz_topic_template)];
	strcpy(topic, zz_topic_template);
	random_topic_name(topic);

	const size_t out_topic_len = (size_t)print_expected_topic(
			NULL, 0, consumer_uuid, topic);
	char out_topic[out_topic_len];
	print_expected_topic(out_topic, out_topic_len, consumer_uuid, topic);

	const size_t uri_len = (size_t)print_expected_url(
			NULL, 0, consumer_uuid, topic);
	char uri[uri_len];
	print_expected_url(uri, uri_len, consumer_uuid, topic);

#define MESSAGES                                                               \
	X("{\"client_mac\": \"54:26:96:", check_zero_messages, 0)              \
	X("db:88:01\", \"application_name\": \"wwww\", "                       \
	  "\"sensor_uuid\":\"abc\", \"a\":5}",                                 \
	  check_zz_decoder_simple,                                             \
	  1)                                                                   \
	X("{\"client_mac\": \"", check_zero_messages, 0)                       \
	X("54:26:96:db:88:01\", \"application_name\": \"wwww\", "              \
	  "\"sensor_uuid\":\"abc\", \"a\":5}",                                 \
	  check_zz_decoder_simple,                                             \
	  1)                                                                   \
	/* Free & Check that session has been freed */                         \
	X(NULL, NO_MESSAGES_CHECK, 0)

	struct message_in msgs[] = {
#define X(a, fn, kafka_msgs) {a, sizeof(a) - 1},
			MESSAGES
#undef X
	};

	check_callback_fn callbacks_functions[] = {
#define X(a, fn, kafka_msgs) fn,
			MESSAGES
#undef X
	};

	static const size_t expected_kafka_msgs[] = {
#define X(a, fn, kafka_msgs) kafka_msgs,
			MESSAGES
#undef X
	};

	test_zz_decoder0(listener_cfg,
			 decoder_cfg,
			 &(struct zz_http2k_params){
					 .uri = uri,
					 .consumer_uuid = consumer_uuid,
					 .topic = out_topic,
			 },
			 msgs,
			 callbacks_functions,
			 RD_ARRAYSIZE(msgs),
			 expected_kafka_msgs,
			 NULL);

#undef MESSAGES
}

/** Checks that the decoder can handle to receive the half of a key */
static void test_zz_decoder_half_key() {
	/// @TODO join with all other tests!
	static const char consumer_uuid[] = "abc";
	char topic[sizeof(zz_topic_template)];
	strcpy(topic, zz_topic_template);
	random_topic_name(topic);

	const size_t out_topic_len = (size_t)print_expected_topic(
			NULL, 0, consumer_uuid, topic);
	char out_topic[out_topic_len];
	print_expected_topic(out_topic, out_topic_len, consumer_uuid, topic);

	const size_t uri_len = (size_t)print_expected_url(
			NULL, 0, consumer_uuid, topic);
	char uri[uri_len];
	print_expected_url(uri, uri_len, consumer_uuid, topic);

#define MESSAGES                                                               \
	X("{\"client_", check_zero_messages, 0)                                \
	X("mac\": \"54:26:96:db:88:01\", \"application_name\": \"wwww\", "     \
	  "\"sensor_uuid\":\"abc\", \"a\":5}",                                 \
	  check_zz_decoder_simple,                                             \
	  1)                                                                   \
	X("{\"client_mac", check_zero_messages, 0)                             \
	X("\": \"54:26:96:db:88:01\", \"application_name\": \"wwww\", "        \
	  "\"sensor_uuid\":\"abc\", \"a\":5}",                                 \
	  check_zz_decoder_simple,                                             \
	  1)                                                                   \
	/* Free & Check that session has been freed */                         \
	X(NULL, NO_MESSAGES_CHECK, 0)

	struct message_in msgs[] = {
#define X(a, fn, kafka_msgs) {a, sizeof(a) - 1},
			MESSAGES
#undef X
	};

	check_callback_fn callbacks_functions[] = {
#define X(a, fn, kafka_msgs) fn,
			MESSAGES
#undef X
	};

	static const size_t expected_kafka_msgs[] = {
#define X(a, fn, kafka_msgs) kafka_msgs,
			MESSAGES
#undef X
	};

	test_zz_decoder0(listener_cfg,
			 decoder_cfg,
			 &(struct zz_http2k_params){
					 .uri = uri,
					 .consumer_uuid = consumer_uuid,
					 .topic = out_topic,
			 },
			 msgs,
			 callbacks_functions,
			 RD_ARRAYSIZE(msgs),
			 expected_kafka_msgs,
			 NULL);

#undef MESSAGES
}

/** Test object that don't need to enrich */
static void test_zz_decoder_objects() {
	/// @TODO join with all other tests!
	static const char consumer_uuid[] = "abc";
	char topic[sizeof(zz_topic_template)];
	strcpy(topic, zz_topic_template);
	random_topic_name(topic);

	const size_t out_topic_len = (size_t)print_expected_topic(
			NULL, 0, consumer_uuid, topic);
	char out_topic[out_topic_len];
	print_expected_topic(out_topic, out_topic_len, consumer_uuid, topic);

	const size_t uri_len = (size_t)print_expected_url(
			NULL, 0, consumer_uuid, topic);
	char uri[uri_len];
	print_expected_url(uri, uri_len, consumer_uuid, topic);

#define MESSAGES                                                               \
	X("{\"client_", check_zero_messages, 0)                                \
	X("mac\": \"54:26:96:db:88:01\", \"application_name\": \"wwww\", "     \
	  "\"sensor_uuid\":\"abc\", \"object\":{\"t1\":1}, \"a\":5}",          \
	  check_zz_decoder_object,                                             \
	  1)                                                                   \
	/* Free & Check that session has been freed */                         \
	X(NULL, NO_MESSAGES_CHECK, 0)

	struct message_in msgs[] = {
#define X(a, fn, kafka_msgs) {a, sizeof(a) - 1},
			MESSAGES
#undef X
	};

	check_callback_fn callbacks_functions[] = {
#define X(a, fn, kafka_msgs) fn,
			MESSAGES
#undef X
	};

	static const size_t expected_kafka_msgs[] = {
#define X(a, fn, kafka_msgs) kafka_msgs,
			MESSAGES
#undef X
	};

	test_zz_decoder0(listener_cfg,
			 decoder_cfg,
			 &(struct zz_http2k_params){
					 .uri = uri,
					 .consumer_uuid = consumer_uuid,
					 .topic = out_topic,
			 },
			 msgs,
			 callbacks_functions,
			 RD_ARRAYSIZE(msgs),
			 expected_kafka_msgs,
			 NULL);

#undef MESSAGES
}

int main() {
	// clang-format off
	listener_cfg = assert_json_loads("{"
			  "\"proto\": \"http\","
			  "\"port\": 2057,"
			  "\"mode\": \"epoll\","
			  "\"num_threads\": 2,"
			  "\"decode_as\": \"zz_http2k\""
			"}");
	// clang-format on

	static const struct CMUnitTest tests[] = {
			cmocka_unit_test(test_zz_decoder_simple),
			cmocka_unit_test(test_zz_decoder_simple_def),
			cmocka_unit_test(test_zz_decoder_double),
			cmocka_unit_test(test_zz_decoder_half),
			cmocka_unit_test(test_zz_decoder_half_string),
			cmocka_unit_test(test_zz_decoder_half_key),
			cmocka_unit_test(test_zz_decoder_objects),
	};

	const int cmocka_run_rc = cmocka_run_group_tests(tests, NULL, NULL);
	json_decref(listener_cfg);
	return cmocka_run_rc;
}
