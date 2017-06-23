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

#include "n2k_kafka_tests.h"
#include "zz_http2k_tests.h"

#include <librd/rd.h>

static const char TEMP_TEMPLATE[] = "n2ktXXXXXX";

static const char CONFIG_TEST[] = "{"
				  "\"brokers\": \"localhost\""
				  "}";

static json_t *listener_cfg = NULL;
static json_t *decoder_cfg = NULL;

/// Trying to decode a JSON closing when you still have not open any json
static void test_zz_decoder_closing(void **prk_consumer) {
	/// @TODO join with all other tests!
	static const char consumer_uuid[] = "abc";
	char topic[sizeof(zz_topic_template)];
	strcpy(topic, zz_topic_template);
	random_topic_name(topic);

	const size_t out_topic_len = (size_t)print_expected_topic(
			NULL, 0, consumer_uuid, topic);
	char out_topic[out_topic_len + 1];
	print_expected_topic(
			out_topic, sizeof(out_topic), consumer_uuid, topic);

	const size_t uri_len = (size_t)print_expected_url(
			NULL, 0, consumer_uuid, topic);
	char uri[uri_len + 1];
	print_expected_url(uri, sizeof(uri), consumer_uuid, topic);

	static const struct message_in msgs[] = {
			// clang-format off
		MESSAGE_IN("}{\"client_mac\": \"54:26:96:db:88:02\", "
			   "\"application_name\": \"wwww\", "
			   "\"sensor_uuid\":\"def\", \"a\":5, \"u\":true}",
			   check_zero_messages,
			   0),
		MESSAGE_IN("}}{\"client_mac\": \"54:26:96:db:88:02\", "
			   "\"application_name\": \"wwww\", "
			   "\"sensor_uuid\":\"def\", \"a\":5, \"u\":true}",
			   check_zero_messages,
			   0),
		MESSAGE_IN("}}}{\"client_mac\": \"54:26:96:db:88:02\", "
			   "\"application_name\": \"wwww\", "
			   "\"sensor_uuid\":\"def\", \"a\":5, \"u\":true}",
			   check_zero_messages,
			   0), /* Free & Check that session has been
				  freed */
		MESSAGE_IN(NULL, NO_MESSAGES_CHECK, 0),
			// clang-format on

	};

	test_zz_decoder0(listener_cfg,
			 decoder_cfg,
			 &(struct zz_http2k_params){
					 .uri = uri,
					 .consumer_uuid = consumer_uuid,
					 .topic = out_topic,
			 },
			 msgs,
			 RD_ARRAYSIZE(msgs),
			 *prk_consumer,
			 NULL);
}

/** Test that the system is able to skip non-string keys is we are partitioning
    via client-mac */
int main() {
	decoder_cfg = assert_json_loads("{}");
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
			cmocka_unit_test(test_zz_decoder_closing),
	};

	const int cmocka_run_rc = run_zz_decoder_group_tests(tests);
	json_decref(decoder_cfg);
	json_decref(listener_cfg);
	return cmocka_run_rc;
}
