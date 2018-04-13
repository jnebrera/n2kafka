/*
**
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
** Copyright (C) 2018, Wizzie S.L.
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

#include "kafka_message_array.h"

#include <time.h>

static enum warning_times_pos
kafka_error_to_warning_time_pos(rd_kafka_resp_err_t err) {
#define LAST_WARNING_POS_CASE(RK_ERR, N2K_ERR)                                 \
	case RK_ERR:                                                           \
		return N2K_ERR;

	switch (err) {
		X_RK_M_PRODUCE_ERR(LAST_WARNING_POS_CASE)
	default:
		return LAST_WARNING_TIME__END;
	};
}

void kafka_message_array_produce(rd_kafka_topic_t *rkt,
				 kafka_message_array *array,
				 char *payload_buffer,
				 int rdkafka_flags,
				 kafka_message_array_produce_state *state) {
	assert(rkt);
	assert(array);
	static const time_t alert_threshold = 5 * 60;

	if (0 == kafka_message_array_size(array)) {
		// Nothing to do!
		goto end;
	}

	struct kafka_message_array_internal *karray =
			kafka_message_array_get_internal(array);
	if (payload_buffer) {
		// The payload buffer is shared between all messages, and it
		// needs to be freed when ALL messages has been delivered. So we
		// send that information to librdkafka delivery report callback.
		size_t i;
		for (i = 0; i < karray->count; ++i) {
			karray->msgs[i]._private = karray;
		}
		// stealing karray
		*array = KAFKA_MESSAGE_ARRAY_INITIALIZER;
		karray->payload_buffer = payload_buffer;
	}

	size_t msgs_ok = (size_t)rd_kafka_produce_batch(rkt,
							RD_KAFKA_PARTITION_UA,
							rdkafka_flags,
							karray->msgs,
							karray->count);
	if (likely(msgs_ok == karray->count)) {
		// all OK!
		goto end;
	}

	size_t i;
	for (i = 0; i < karray->count && msgs_ok < karray->count; ++i) {
		int warn = 1;
		if (karray->msgs[i].err == RD_KAFKA_RESP_ERR_NO_ERROR) {
			continue;
		}

		msgs_ok++;
		const size_t last_warning_time_pos =
				kafka_error_to_warning_time_pos(
						karray->msgs[i].err);

		if (state && last_warning_time_pos <= LAST_WARNING_TIME__END) {
			const time_t last_warning_time =
					state->produce_error_last_time
							[last_warning_time_pos];
			const time_t now = time(NULL);
			if (difftime(now, last_warning_time) <
			    alert_threshold) {
				warn = 0;
			} else {
				state->produce_error_last_time
						[last_warning_time_pos] = now;
			}
		}

		if (warn) {
			rdlog(LOG_ERR,
			      "Can't produce to topic %s: %s",
			      rd_kafka_topic_name(rkt),
			      rd_kafka_err2str(karray->msgs[i].err));
		}

		kafka_message_array_internal_decref(karray);
	}

end:
	kafka_msg_array_done(array);
}
