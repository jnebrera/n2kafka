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

#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#include <cmocka.h>

struct test_listener {
	const char *decoder;
	const char *proto;
	int num_threads;
	uint16_t port;
};

struct test_messages {
	const char *send_msg;
	const char *uri;
	const char *topic;
	const char *expected_http_response;
	const char **expected_kafka_messages;
	const long http_response_code;
	const size_t listener_idx; ///< Expected listener to receive message
};

/// Test messages iteration
struct test_iteration {
#define TEST_ITERATION_MAGIC 0x341E13A141E13A1
	uint64_t magic;
	struct test_listener *listeners;
	const char *default_topic;

	struct test_messages *test_messages;
};

// Point topic here to generate it randomly
extern char test_random_topic[];

void test_n2k(void **state);
int n2k_group_setup(void **state);
int n2k_group_teardown(void **state);

#define n2k_test(t_state) cmocka_unit_test_prestate(test_n2k, t_state)
#define n2k_run_group_tests(t_tests)                                           \
	cmocka_run_group_tests(t_tests, n2k_group_setup, n2k_group_teardown)
