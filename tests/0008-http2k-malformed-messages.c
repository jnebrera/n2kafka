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

#include <assert.h>
#include <cmocka.h>
#include <setjmp.h>

static const char TEMP_TEMPLATE[] = "n2ktXXXXXX";

static const char CONFIG_TEST[] = "{"
				  "\"brokers\": \"localhost\","
				  "\"zz_http2k_config\": {"
				  "\"sensors_uuids\" : {"
				  "\"abc\" : {"
				  "\"enrichment\":{"
				  "\"sensor_uuid\":\"abc\","
				  "\"a\":1,"
				  "\"b\":\"c\","
				  "\"d\":true,"
				  "\"e\":null"
				  "}"
				  "},"
				  "\"def\" : {"
				  "\"enrichment\":{"
				  "\"sensor_uuid\":\"def\","
				  "\"f\":1,"
				  "\"g\":\"w\","
				  "\"h\":false,"
				  "\"i\":null,"
				  "\"j\":2.5"
				  "}"
				  "},"
				  "\"ghi\" : {"
				  "\"enrichment\":{"
				  "\"o\": {"
				  "\"a\":90"
				  "}"
				  "}"
				  "},"
				  "\"jkl\" : {"
				  "\"enrichment\":{"
				  "\"v\":[1,2,3,4,5]"
				  "}"
				  "}"
				  "},"
				  "\"topics\" : {"
				  "\"rb_flow\": {"
				  "},"
				  "\"rb_event\": {"
				  "}"
				  "}"
				  "}"
				  "}";

static void prepare_args(const char *topic,
			 const char *sensor_uuid,
			 const char *client_ip,
			 struct pair *mem,
			 size_t memsiz,
			 keyval_list_t *list) {
	assert(3 == memsiz);
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

/// Trying to decode a JSON closing when you still have not open any json
static void test_zz_decoder_closing() {
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
	X("}{\"client_mac\": \"54:26:96:db:88:02\", "                          \
	  "\"application_name\": \"wwww\", \"sensor_uuid\":\"def\", "          \
	  "\"a\":5, \"u\":true}",                                              \
	  check_zero_messages)                                                 \
	X("}}{\"client_mac\": \"54:26:96:db:88:02\", "                         \
	  "\"application_name\": \"wwww\", \"sensor_uuid\":\"def\", "          \
	  "\"a\":5, \"u\":true}",                                              \
	  check_zero_messages)                                                 \
	X("}}}{\"client_mac\": \"54:26:96:db:88:02\", "                        \
	  "\"application_name\": \"wwww\", \"sensor_uuid\":\"def\", "          \
	  "\"a\":5, \"u\":true}",                                              \
	  check_zero_messages)                                                 \
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

/** Test that the system is able to skip non-string keys is we are partitioning
    via client-mac */
int main() {
	const struct CMUnitTest tests[] = {
			cmocka_unit_test(test_zz_decoder_closing),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
