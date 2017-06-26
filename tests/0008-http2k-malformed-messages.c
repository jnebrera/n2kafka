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

/** Test that the system is able to skip non-string keys is we are partitioning
    via client-mac */
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
		zz_decoder_test("json_unexpected_closing",
			ZZ_TESTS_PARAMS(.consumer_uuid = "abc",
				.listener_conf = listener_cfg),
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
		),
	};

	const int cmocka_run_rc = run_zz_decoder_group_tests(tests);
	json_decref(listener_cfg);
	return cmocka_run_rc;
}
