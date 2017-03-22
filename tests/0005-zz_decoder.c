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

#include "rb_json_tests.c"
#include "zz_http2k_tests.c"

#include "../src/listener/http.c"

static const char CONFIG_TEST[] = "{"
				  "\"brokers\": \"kafka\","
				  "\"zz_http2k_config\": {"
				  "\"topics\" : {"
				  "\"rb_flow\": {"
				  "\"partition_key\":\"client_mac\","
				  "\"partition_algo\":\"mac\""
				  "},"
				  "\"rb_event\": {"
				  "}"
				  "}"
				  "}"
				  "}";

static const char *VALID_URL = "/rbdata/abc/rb_flow";

static void test_validate_uri() {
	test_zz_decoder_setup(CONFIG_TEST);

	int allok = 1;
	char *topic = NULL, *uuid = NULL;
	int validation_rc = zz_http2k_validation(
			NULL /* @TODO this should change */,
			VALID_URL,
			&allok,
			&topic,
			&uuid,
			"test_ip");

	assert_true(MHD_YES == validation_rc);
	assert_true(0 == strcmp(topic, "rb_flow"));
	assert_true(0 == strcmp(uuid, "abc"));

	free(topic);
	free(uuid);

	test_zz_decoder_teardown();
}

static void prepare_args(const char *topic,
			 const char *sensor_uuid,
			 const char *client_ip,
			 struct pair *mem,
			 size_t memsiz,
			 keyval_list_t *list) {
	assert_true(3 == memsiz);
	memset(mem, 0, sizeof(*mem) * 3);

	mem[0].key = "topic";
	mem[0].value = topic;
	mem[1].key = "sensor_uuid";
	mem[1].value = sensor_uuid;
	mem[2].key = "client_ip";
	mem[2].value = client_ip;

	add_key_value_pair(list, &mem[0]);
	add_key_value_pair(list, &mem[1]);
	add_key_value_pair(list, &mem[2]);
}

static void check_zz_decoder_double0(struct zz_session **sess,
				     void *unused __attribute__((unused)),
				     size_t expected_size) {
	size_t i = 0;
	rd_kafka_message_t rkm[2];
	json_error_t jerr;
	const char *client_mac, *application_name, *sensor_uuid;
	json_int_t a;

	assert_true(expected_size == rd_kafka_msg_q_size(&(*sess)->msg_queue));
	rd_kafka_msg_q_dump(&(*sess)->msg_queue, rkm);
	assert_true(0 == rd_kafka_msg_q_size(&(*sess)->msg_queue));

	for (i = 0; i < expected_size; ++i) {
		json_t *root = json_loadb(rkm[i].payload, rkm[i].len, 0, &jerr);
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
		free(rkm[i].payload);
	}
}

static void check_zz_decoder_simple(struct zz_session **sess, void *opaque) {
	check_zz_decoder_double0(sess, opaque, 1);
}

static void check_zz_decoder_double(struct zz_session **sess, void *opaque) {
	check_zz_decoder_double0(sess, opaque, 2);
}

static void check_zz_decoder_simple_def(struct zz_session **sess,
					void *unused __attribute__((unused))) {
	rd_kafka_message_t rkm;
	json_error_t jerr;
	const char *client_mac, *application_name, *sensor_uuid;
	int u;

	assert_true(1 == rd_kafka_msg_q_size(&(*sess)->msg_queue));
	rd_kafka_msg_q_dump(&(*sess)->msg_queue, &rkm);
	assert_true(0 == rd_kafka_msg_q_size(&(*sess)->msg_queue));

	json_t *root = json_loadb(rkm.payload, rkm.len, 0, &jerr);
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
	free(rkm.payload);
}

static void check_zz_decoder_object(struct zz_session **sess,
				    void *unused __attribute__((unused))) {
	rd_kafka_message_t rkm;
	json_error_t jerr;
	const char *client_mac, *application_name, *sensor_uuid;
	json_int_t a, t1;

	assert_true(1 == rd_kafka_msg_q_size(&(*sess)->msg_queue));
	rd_kafka_msg_q_dump(&(*sess)->msg_queue, &rkm);
	assert_true(0 == rd_kafka_msg_q_size(&(*sess)->msg_queue));

	json_t *root = json_loadb(rkm.payload, rkm.len, 0, &jerr);
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
	free(rkm.payload);
}

static void test_zz_decoder_simple() {
	struct pair mem[3];
	keyval_list_t args;
	keyval_list_init(&args);
	prepare_args("rb_flow",
		     "abc",
		     "127.0.0.1",
		     mem,
		     RD_ARRAYSIZE(mem),
		     &args);

#define MESSAGES                                                               \
	X("{\"client_mac\": \"54:26:96:db:88:01\", "                           \
	  "\"application_name\": \"wwww\", \"sensor_uuid\":\"abc\", \"a\":5}", \
	  check_zz_decoder_simple)                                             \
	/* Free & Check that session has been freed */                         \
	X(NULL, check_null_session)

	struct message_in msgs[] = {
#define X(a, fn) {a, sizeof(a) - 1},
			MESSAGES
#undef X
	};

	check_callback_fn callbacks_functions[] = {
#define X(a, fn) fn,
			MESSAGES
#undef X
	};

	test_zz_decoder0(CONFIG_TEST,
			 &args,
			 msgs,
			 callbacks_functions,
			 RD_ARRAYSIZE(msgs),
			 NULL);

#undef MESSAGES
}

/// Simple decoding with another enrichment
static void test_zz_decoder_simple_def() {
	struct pair mem[3];
	keyval_list_t args;
	keyval_list_init(&args);
	prepare_args("rb_flow",
		     "def",
		     "127.0.0.1",
		     mem,
		     RD_ARRAYSIZE(mem),
		     &args);

#define MESSAGES                                                               \
	X("{\"client_mac\": \"54:26:96:db:88:02\", "                           \
	  "\"application_name\": \"wwww\", \"sensor_uuid\":\"def\", "          \
	  "\"a\":5, \"u\":true}",                                              \
	  check_zz_decoder_simple_def)                                         \
	/* Free & Check that session has been freed */                         \
	X(NULL, check_null_session)

	struct message_in msgs[] = {
#define X(a, fn) {a, sizeof(a) - 1},
			MESSAGES
#undef X
	};

	check_callback_fn callbacks_functions[] = {
#define X(a, fn) fn,
			MESSAGES
#undef X
	};

	test_zz_decoder0(CONFIG_TEST,
			 &args,
			 msgs,
			 callbacks_functions,
			 RD_ARRAYSIZE(msgs),
			 NULL);

#undef MESSAGES
}

/** Two messages in the same input string */
static void test_zz_decoder_double() {
	struct pair mem[3];
	keyval_list_t args;
	keyval_list_init(&args);
	prepare_args("rb_flow",
		     "abc",
		     "127.0.0.1",
		     mem,
		     RD_ARRAYSIZE(mem),
		     &args);

#define MESSAGES                                                               \
	X("{\"client_mac\": \"54:26:96:db:88:01\", "                           \
	  "\"application_name\": \"wwww\", \"sensor_uuid\":\"abc\", \"a\":5}"  \
	  "{\"client_mac\": \"54:26:96:db:88:02\", "                           \
	  "\"application_name\": \"wwww\", \"sensor_uuid\":\"abc\", \"a\":5}", \
	  check_zz_decoder_double)                                             \
	/* Free & Check that session has been freed */                         \
	X(NULL, check_null_session)

	struct message_in msgs[] = {
#define X(a, fn) {a, sizeof(a) - 1},
			MESSAGES
#undef X
	};

	check_callback_fn callbacks_functions[] = {
#define X(a, fn) fn,
			MESSAGES
#undef X
	};

	test_zz_decoder0(CONFIG_TEST,
			 &args,
			 msgs,
			 callbacks_functions,
			 RD_ARRAYSIZE(msgs),
			 NULL);

#undef MESSAGES
}

static void test_zz_decoder_half() {
	struct pair mem[3];
	keyval_list_t args;
	keyval_list_init(&args);
	prepare_args("rb_flow",
		     "abc",
		     "127.0.0.1",
		     mem,
		     RD_ARRAYSIZE(mem),
		     &args);

#define MESSAGES                                                               \
	X("{\"client_mac\": \"54:26:96:db:88:01\", ", check_zero_messages)     \
	X("\"application_name\": \"wwww\", \"sensor_uuid\":\"abc\", \"a\":5}", \
	  check_zz_decoder_simple)                                             \
	/* Free & Check that session has been freed */                         \
	X(NULL, check_null_session)

	struct message_in msgs[] = {
#define X(a, fn) {a, sizeof(a) - 1},
			MESSAGES
#undef X
	};

	check_callback_fn callbacks_functions[] = {
#define X(a, fn) fn,
			MESSAGES
#undef X
	};

	test_zz_decoder0(CONFIG_TEST,
			 &args,
			 msgs,
			 callbacks_functions,
			 RD_ARRAYSIZE(msgs),
			 NULL);

#undef MESSAGES
}

/** Checks that the decoder can handle to receive the half of a string */
static void test_zz_decoder_half_string() {
	struct pair mem[3];
	keyval_list_t args;
	keyval_list_init(&args);
	prepare_args("rb_flow",
		     "abc",
		     "127.0.0.1",
		     mem,
		     RD_ARRAYSIZE(mem),
		     &args);

#define MESSAGES                                                               \
	X("{\"client_mac\": \"54:26:96:", check_zero_messages)                 \
	X("db:88:01\", \"application_name\": \"wwww\", "                       \
	  "\"sensor_uuid\":\"abc\", \"a\":5}",                                 \
	  check_zz_decoder_simple)                                             \
	X("{\"client_mac\": \"", check_zero_messages)                          \
	X("54:26:96:db:88:01\", \"application_name\": \"wwww\", "              \
	  "\"sensor_uuid\":\"abc\", \"a\":5}",                                 \
	  check_zz_decoder_simple)                                             \
	/* Free & Check that session has been freed */                         \
	X(NULL, check_null_session)

	struct message_in msgs[] = {
#define X(a, fn) {a, sizeof(a) - 1},
			MESSAGES
#undef X
	};

	check_callback_fn callbacks_functions[] = {
#define X(a, fn) fn,
			MESSAGES
#undef X
	};

	test_zz_decoder0(CONFIG_TEST,
			 &args,
			 msgs,
			 callbacks_functions,
			 RD_ARRAYSIZE(msgs),
			 NULL);

#undef MESSAGES
}

/** Checks that the decoder can handle to receive the half of a key */
static void test_zz_decoder_half_key() {
	struct pair mem[3];
	keyval_list_t args;
	keyval_list_init(&args);
	prepare_args("rb_flow",
		     "abc",
		     "127.0.0.1",
		     mem,
		     RD_ARRAYSIZE(mem),
		     &args);

#define MESSAGES                                                               \
	X("{\"client_", check_zero_messages)                                   \
	X("mac\": \"54:26:96:db:88:01\", \"application_name\": \"wwww\", "     \
	  "\"sensor_uuid\":\"abc\", \"a\":5}",                                 \
	  check_zz_decoder_simple)                                             \
	X("{\"client_mac", check_zero_messages)                                \
	X("\": \"54:26:96:db:88:01\", \"application_name\": \"wwww\", "        \
	  "\"sensor_uuid\":\"abc\", \"a\":5}",                                 \
	  check_zz_decoder_simple)                                             \
	/* Free & Check that session has been freed */                         \
	X(NULL, check_null_session)

	struct message_in msgs[] = {
#define X(a, fn) {a, sizeof(a) - 1},
			MESSAGES
#undef X
	};

	check_callback_fn callbacks_functions[] = {
#define X(a, fn) fn,
			MESSAGES
#undef X
	};

	test_zz_decoder0(CONFIG_TEST,
			 &args,
			 msgs,
			 callbacks_functions,
			 RD_ARRAYSIZE(msgs),
			 NULL);

#undef MESSAGES
}

/** Test object that don't need to enrich */
static void test_zz_decoder_objects() {
	struct pair mem[3];
	keyval_list_t args;
	keyval_list_init(&args);
	prepare_args("rb_flow",
		     "abc",
		     "127.0.0.1",
		     mem,
		     RD_ARRAYSIZE(mem),
		     &args);

#define MESSAGES                                                               \
	X("{\"client_", check_zero_messages)                                   \
	X("mac\": \"54:26:96:db:88:01\", \"application_name\": \"wwww\", "     \
	  "\"sensor_uuid\":\"abc\", \"object\":{\"t1\":1}, \"a\":5}",          \
	  check_zz_decoder_object)                                             \
	/* Free & Check that session has been freed */                         \
	X(NULL, check_null_session)

	struct message_in msgs[] = {
#define X(a, fn) {a, sizeof(a) - 1},
			MESSAGES
#undef X
	};

	check_callback_fn callbacks_functions[] = {
#define X(a, fn) fn,
			MESSAGES
#undef X
	};

	test_zz_decoder0(CONFIG_TEST,
			 &args,
			 msgs,
			 callbacks_functions,
			 RD_ARRAYSIZE(msgs),
			 NULL);

#undef MESSAGES
}

int main() {
	const struct CMUnitTest tests[] = {
			cmocka_unit_test(test_validate_uri),
			cmocka_unit_test(test_zz_decoder_simple),
			cmocka_unit_test(test_zz_decoder_simple_def),
			cmocka_unit_test(test_zz_decoder_double),
			cmocka_unit_test(test_zz_decoder_half),
			cmocka_unit_test(test_zz_decoder_half_string),
			cmocka_unit_test(test_zz_decoder_half_key),
			cmocka_unit_test(test_zz_decoder_objects),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
