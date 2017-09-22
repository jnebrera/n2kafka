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

int main() {
	const char test_msg[] = "{\"test1\":1}";

	// clang-format off
	struct test_iteration test1[] = {
	{
		.magic = TEST_ITERATION_MAGIC,


		.listeners = (struct test_listener[]){
			{.proto = "http", .decoder = "meraki"},
			{},
		},
		.default_topic = test_random_topic,
		.test_messages = (struct test_messages[]){
			{
				.send_msg = test_msg,
				.uri = "/",
				.http_response_code = 200,
				.topic = test_random_topic,
				.listener_idx = 0,
				.expected_kafka_messages =
					(const char *[]){test_msg, NULL},
			}, {
				.uri = "/v1/meraki/myowntestvalidator",
				.http_response_code = 200,
				.topic = test_random_topic,
				.listener_idx = 0,
				.expected_http_response = "myowntestvalidator",
				.expected_kafka_messages =
					(const char *[]){NULL},
			},
			{}
		}
	},
	{}};
	// clang-format on

	const struct CMUnitTest tests[] = {
			n2k_test(&test1),
	};
	return n2k_run_group_tests(tests);
}
