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

#include <assert.h>
#include <cmocka.h>
#include <setjmp.h>

static const char TEMP_TEMPLATE[] = "n2ktXXXXXX";

static const char CONFIG_TEST[] = "{"
				  "\"brokers\": \"localhost\","
				  "\"zz_http2k_config\": {"
				  "\"sensors_uuids\" : {"
				  "\"abc\" : {"
				  "\"enrichment\": {"
				  "\"sensor_uuid\":\"abc\","
				  "\"a\":1,"
				  "\"b\":\"c\","
				  "\"d\":true,"
				  "\"e\":null"
				  "}"
				  "},"
				  "\"def\" : {"
				  "\"enrichment\": {"
				  "\"sensor_uuid\":\"def\","
				  "\"f\":1,"
				  "\"g\":\"w\","
				  "\"h\":false,"
				  "\"i\":null,"
				  "\"j\":2.5"
				  "}"
				  "},"
				  "\"ghi\" : {"
				  "\"enrichment\": {"
				  "\"o\": {"
				  "\"a\":90"
				  "}"
				  "}"
				  "},"
				  "\"jkl\" : {"
				  "\"enrichment\": {"
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

static void validate_test(void (*f)(struct zz_config *rb)) {
	init_global_config();
	char temp_filename[sizeof(TEMP_TEMPLATE)];
	strcpy(temp_filename, TEMP_TEMPLATE);
	int temp_fd = mkstemp(temp_filename);
	assert(temp_fd >= 0);
	write(temp_fd, CONFIG_TEST, strlen(CONFIG_TEST));

	parse_config(temp_filename);
	unlink(temp_filename);

	f(&global_config.rb);

	free_global_config();

	close(temp_fd);
}

static void validate_topic_test0(struct zz_config *rb) {
	size_t i;
	struct {
		const char *topic;
		int expected;
	} validations[] = {
			{.topic = "rb_flow", .expected = 1},
			{.topic = "rb_event", .expected = 1},
			{.topic = "rb_unknown", .expected = 0},
			{.topic = "rb_anotherone", .expected = 0},
	};

	for (i = 0; i < sizeof(validations) / sizeof(validations[0]); ++i) {
		const int result = zz_http2k_validate_topic(
				&rb->database, validations[i].topic);
		assert(validations[i].expected == result);
	}
}

static void validate_topic_test() {
	validate_test(validate_topic_test0);
}

static void validate_uuid_test0(struct zz_config *rb) {
	size_t i;
	struct {
		const char *uuid;
		int expected;
	} validations[] = {
			{.uuid = "abc", .expected = 1},
			{.uuid = "def", .expected = 1},
			{.uuid = "ghi", .expected = 1},
			{.uuid = "jkl", .expected = 1},
			{.uuid = "mno", .expected = 0},
	};

	for (i = 0; i < sizeof(validations) / sizeof(validations[0]); ++i) {
		const int result = zz_http2k_validate_uuid(&rb->database,
							   validations[i].uuid);
		assert(validations[i].expected == result);
	}
}

static void validate_uuid_test() {
	validate_test(validate_uuid_test0);
}

#if 0
int zz_http2k_validate_topic(struct zz_database *db, const char *topic) {
	pthread_rwlock_rdlock(&db->rwlock);
	const int ret = topics_db_topic_exists(db->topics_db,topic);
	pthread_rwlock_unlock(&db->rwlock);
}
#endif

int main() {
	const struct CMUnitTest tests[] = {
			cmocka_unit_test(validate_uuid_test),
			cmocka_unit_test(validate_topic_test),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);

	return 0;
}
