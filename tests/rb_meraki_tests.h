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

#include "../src/decoder/meraki/rb_meraki.c"
#include "rb_json_tests.c"

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#define RB_UNUSED __attribute__((unused))

static int meraki_global_done(void **state) {
	(void)state;
	meraki_decoder.done();
	return 0;
}

static void
MerakiDecoder_test_base(const char *config_str,
			const char *secrets,
			const char *msg,
			const struct checkdata_array *checkdata) RB_UNUSED;
static void MerakiDecoder_test_base(const char *config_str,
				    const char *secrets,
				    const char *msg,
				    const struct checkdata_array *checkdata) {
	size_t i;
	const char *topic_name = NULL;
	json_error_t jerr;
	struct meraki_decoder_info decoder_info;
	json_t *config = NULL;

	meraki_decoder_info_create(&decoder_info);

	if (config_str) {
		config = json_loads(config_str, 0, NULL);
		assert_true(config);
		parse_meraki_decoder_info(&decoder_info, &topic_name, config);
		assert_true(decoder_info.per_listener_enrichment);
	}

	json_t *meraki_secrets_array =
			json_loadb(secrets, strlen(secrets), 0, &jerr);
	assert_true(meraki_secrets_array);

	const int parse_rc = meraki_decoder.reload(meraki_secrets_array);
	assert_true(parse_rc == 0);
	json_decref(meraki_secrets_array);

	struct kafka_message_array *notifications_array = process_meraki_buffer(
			msg, strlen(msg), "127.0.0.1", &decoder_info);

	if (checkdata) {
		rb_assert_json_array(notifications_array->msgs,
				     notifications_array->count,
				     checkdata);

		for (i = 0; i < notifications_array->count; ++i)
			free(notifications_array->msgs[i].payload);
		free(notifications_array);
	} else {
		assert_true(0 == notifications_array);
	}

	meraki_decoder_info_destructor(&decoder_info);
	if (config) {
		json_decref(config);
	}
}
