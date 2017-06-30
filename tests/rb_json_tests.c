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

// rb_json_tests.c
#include <jansson.h>
#include <librd/rd.h>
#include <librd/rdfloat.h>
#include <librdkafka/rdkafka.h>

#include <setjmp.h> // This needs to be before cmocka.h

#include <cmocka.h>

#include <stdarg.h>
#include <stddef.h>
#include <string.h>

struct checkdata_value {
	const char *key;
	json_type type;
	const char *value;
};

struct checkdata {
	size_t size;
	const struct checkdata_value *checks;
};

#define CHECKDATA(name, ...)                                                   \
	static const struct checkdata_value name##checks[] = {__VA_ARGS__};    \
	static const struct checkdata name = {.size = sizeof(name##checks) /   \
						      sizeof(name##checks[0]), \
					      .checks = name##checks}

struct checkdata_array {
	size_t size;
	const struct checkdata **checks;
};

#define CHECKDATA_ARRAY(name, ...)                                             \
	static const struct checkdata *name##elms[] = {__VA_ARGS__};           \
	static const struct checkdata_array name = {                           \
			.checks = name##elms,                                  \
			.size = sizeof(name##elms) / sizeof(name##elms[0]),    \
	}

static void assert_trueEqual(const int64_t a,
			     const int64_t b,
			     const char *key,
			     const char *src) __attribute__((unused));
static void assert_trueEqual(const int64_t a,
			     const int64_t b,
			     const char *key,
			     const char *src) {
	if (a != b) {
		fprintf(stderr,
			"[%s integer value mismatch] Actual: %ld, Expected: "
			"%ld in %s\n",
			key,
			a,
			b,
			src);
		assert_true(a == b);
	}
}

static void
rb_assert_meraki_client_latlon(const char *coord1, const char *coord2) {
	// clang-format off
	struct {
		const char *str;
		double latitude, longitude;
	} coordinates[] = {
		{
			.str = coord1,
		},{
			.str = coord2,
	}};
	// clang-format on

	size_t i;
	for (i = 0; i < RD_ARRAYSIZE(coordinates); ++i) {
		const int sscanf_rc = sscanf(coordinates[i].str,
					     "%lf,%lf",
					     &coordinates[i].latitude,
					     &coordinates[i].longitude);
		assert_return_code(sscanf_rc, errno);
	}

	assert_true(rd_deq0(coordinates[0].latitude,
			    coordinates[1].latitude,
			    0.001) &&
		    rd_deq0(coordinates[0].longitude,
			    coordinates[1].longitude,
			    0.001));
}

static void rb_assert_json_value(const struct checkdata_value *chk_value,
				 const json_t *json_value,
				 const char *src) {
	// assert_true(chk_value->type == json_typeof(json_value));
	if (chk_value->value == NULL && json_value == NULL) {
		return; // All ok
	}

	if (chk_value->value == NULL && json_value != NULL) {
		fprintf(stderr,
			"Json key %s with value %s, should not exists in "
			"(%s)\n",
			chk_value->key,
			json_string_value(json_value),
			src);
		assert_true(!json_value);
	}

	if (NULL == json_value) {
		fprintf(stderr,
			"Json value %s does not exists in %s\n",
			chk_value->key,
			src);
		assert_true(json_value);
	}
	switch (json_typeof(json_value)) {
	case JSON_INTEGER: {
		const json_int_t json_int_value =
				json_integer_value(json_value);
		const long chk_int_value = atol(chk_value->value);
		assert_trueEqual(json_int_value,
				 chk_int_value,
				 chk_value->key,
				 src);
	} break;
	case JSON_STRING: {
		const char *json_str_value = json_string_value(json_value);
		// Special case for double precision.
		if (0 == strcmp("client_latlong", chk_value->key)) {
			rb_assert_meraki_client_latlon(json_str_value,
						       chk_value->value);
		} else {
			assert_true(0 ==
				    strcmp(json_str_value, chk_value->value));
		}
	} break;
	default:
		assert_true(!"You should not be here");
	}
}

static json_t *rb_assert_json_loadb(const char *buf, size_t buflen) {
	json_error_t error;
	json_t *root = json_loadb(buf, buflen, 0, &error);

	if (root == NULL) {
		fail_msg("[EROR PARSING JSON][reason:%s][source:%s]\n",
			 error.text,
			 error.source);
	}

	return root;
}

static void
rb_assert_json_n(const char *str, size_t sz, const struct checkdata *checkdata)
		__attribute__((unused));
static void rb_assert_json_n(const char *str,
			     size_t sz,
			     const struct checkdata *checkdata) {
	size_t i = 0;
	json_t *root = rb_assert_json_loadb(str, sz);

	for (i = 0; i < checkdata->size; ++i) {
		const json_t *json_value =
				json_object_get(root, checkdata->checks[i].key);
		rb_assert_json_value(&checkdata->checks[i], json_value, str);
	}

	json_decref(root);
}

static void rb_assert_json(const char *str, const struct checkdata *checkdata)
		__attribute__((unused));
static void rb_assert_json(const char *str, const struct checkdata *checkdata) {
	rb_assert_json_n(str, strlen(str), checkdata);
}

static void rb_assert_json_array(const rd_kafka_message_t *msgs,
				 size_t msgs_size,
				 const struct checkdata_array *checkdata_array)
		__attribute__((unused));
static void
rb_assert_json_array(const rd_kafka_message_t *msgs,
		     size_t msgs_size,
		     const struct checkdata_array *checkdata_array) {

	size_t i;

	assert_true(msgs_size == checkdata_array->size);
	for (i = 0; i < checkdata_array->size; ++i) {
		size_t payload_size = msgs[i].len;
		rb_assert_json_n(msgs[i].payload,
				 payload_size,
				 checkdata_array->checks[i]);
	}
}
