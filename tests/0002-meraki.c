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

#include "n2k_base_test.h"

// clang-format off
#define MERAKI_BASE_TEST(t_listener_topic, t_default_topic, t_expected_topic)  \
{{                                                                             \
	.magic = TEST_ITERATION_MAGIC,                                         \
	.listeners = (struct test_listener[]){                                 \
		{ .proto = "http",                                             \
		  .decoder = "meraki",                                         \
		  .topic = t_listener_topic,                                   \
		},                                                             \
		{},                                                            \
	},                                                                     \
	.default_topic = t_default_topic,                                      \
	.test_messages = (struct test_messages[]) {                            \
		{ .send_msg = test_msg,                                        \
		  .uri = "/",                                                  \
		  .http_response_code = 200,                                   \
		  .topic = t_expected_topic,                                    \
		  .listener_idx = 0,                                           \
		  .expected_kafka_messages = (const char *[]){                 \
			  test_msg,                                            \
			  NULL                                                 \
		  },                                                           \
		},                                                             \
		{ .uri = "/v1/meraki/myowntestvalidator",                      \
		  .http_response_code = 200,                                   \
		  .topic = NULL,                                               \
		  .listener_idx = 0,                                           \
		  .expected_http_response = "myowntestvalidator",              \
		  .expected_kafka_messages = (const char *[]){NULL},           \
		},                                                             \
		{}                                                             \
	}                                                                      \
},{}}
// clang-format on

int main() {
	const char test_msg[] = "{\"test1\":1}";
	const char *unused_topic = random_topic();

	struct test_iteration test_default_topic[] = MERAKI_BASE_TEST(
			NULL, test_random_topic, test_random_topic);
	struct test_iteration test_listener_topic[] = MERAKI_BASE_TEST(
			test_random_topic, NULL, test_random_topic);
	struct test_iteration test_both_topic[] = MERAKI_BASE_TEST(
			test_random_topic, unused_topic, test_random_topic);

	const struct CMUnitTest tests[] = {
			n2k_test(test_default_topic),
			n2k_test(test_listener_topic),
			n2k_test(test_both_topic),
	};
	return n2k_run_group_tests(tests);
}
