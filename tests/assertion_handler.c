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

#include "assertion_handler.h"

#include <setjmp.h>

#include <cmocka.h>
#include <jansson.h>
#include <librd/rd.h>

#include <assert.h>

/**
 * Creates a new assertion_handler
 * @return assertion_handler The new assertion handler
 */
void assertion_handler_new(struct assertion_handler_s *this) {
	STAILQ_INIT(&this->assertion_q);
}

/**
 * Inserts an assertion at the end of the assertion queue
 * @param assertion_handler assertion_handler_s to add the assertion
 * @param assertion         New assertion to add
 */
void assertion_handler_push_assertion(
		struct assertion_handler_s *assertion_handler,
		const char *str) {

	const size_t str_len = strlen(str);
	struct assertion_e *assertion =
			malloc(sizeof(*assertion) + str_len + 1);
	assert_non_null(assertion);
	memcpy(assertion->str, str, str_len + 1);
	STAILQ_INSERT_TAIL(&assertion_handler->assertion_q, assertion, tailq);
}

/**
 * Check element against the first push assertion
 */
void assertion_handler_assert(struct assertion_handler_s *assertion_handler,
			      const char *str,
			      size_t str_len) {

	struct assertion_e *assertion =
			STAILQ_FIRST(&assertion_handler->assertion_q);
	STAILQ_REMOVE_HEAD(&assertion_handler->assertion_q, tailq);
	assert_non_null(assertion);

	struct {
		json_error_t jerr;
		json_t *json;
		const char *msg;
	} test_msgs[] = {{.json = json_loadb(str,
					     str_len,
					     0 /* flags */,
					     &test_msgs[0].jerr)},
			 {.json = json_loads(assertion->str,
					     0 /* flags */,
					     &test_msgs[1].jerr)}};

	size_t i;
	for (i = 0; i < RD_ARRAYSIZE(test_msgs); ++i) {
		const char *msg_type = i == 0 ? "test value"
					      : "kafka received message";
		if (NULL == test_msgs[i].json) {
			fail_msg("Couldn't load %s test json: %s",
				 msg_type,
				 test_msgs[i].jerr.text);
		}

		const int unpack_rc = json_unpack_ex(test_msgs[i].json,
						     &test_msgs[i].jerr,
						     JSON_STRICT,
						     "{s:s}",
						     "message",
						     &test_msgs[i].msg);

		if (unpack_rc != 0) {
			fail_msg("Couldn't extract %s message child: %s",
				 msg_type,
				 test_msgs[i].jerr.text);
		}
	}

	assert_string_equal(test_msgs[0].msg, test_msgs[1].msg);

	for (i = 0; i < RD_ARRAYSIZE(test_msgs); ++i) {
		json_decref(test_msgs[i].json);
	}

	free(assertion);
}
