/*
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
** Author: Eugenio Perez <eupm90@gmail.com>
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

#include "dumb.h"

#include "util/kafka.h"

static void dumb_decode(const char *buffer,
			size_t buf_size,
			const keyval_list_t *keyval,
			void *listener_callback_opaque,
			const char **response,
			size_t *response_size,
			void *sessionp) {
	(void)keyval;
	(void)response;
	(void)response_size;
	(void)sessionp;

	rd_kafka_topic_t *rkt =
			new_rkt_global_config(default_topic_name(), NULL);

	// const casting
	char *mutable_buffer;
	memcpy(&mutable_buffer, &buffer, sizeof(mutable_buffer));

	send_to_kafka(rkt,
		      mutable_buffer,
		      buf_size,
		      RD_KAFKA_MSG_F_COPY,
		      listener_callback_opaque);
	rd_kafka_topic_destroy(rkt);
}

static const char *dumb_decoder_name() {
	return "";
}

const struct n2k_decoder dumb_decoder = {
		.name = dumb_decoder_name,
		.callback = dumb_decode,
};
