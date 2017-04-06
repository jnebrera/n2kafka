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

#include "listener/http.c"
#include "util/kafka.h"

#include "n2k_kafka_tests.h"

#include <setjmp.h> // This needs to be before cmocka.h

#include <cmocka.h>

#include <fcntl.h>
#include <netinet/in.h>

static const char ZZ_LOCK_FILE[] = "n2kt_listener.lck";

void test_zz_decoder_setup(struct zz_test_state *state,
			   const json_t *decoder_conf) {
	assert_non_null(state);
	(void)decoder_conf;
	char errstr[512];

	init_global_config();
	global_config.brokers = strdup("kafka:9092");
	const rd_kafka_conf_res_t broker_rc =
			rd_kafka_conf_set(global_config.kafka_conf,
					  "metadata.broker.list",
					  global_config.brokers,
					  errstr,
					  sizeof(errstr));
	if (broker_rc != RD_KAFKA_CONF_OK) {
		fail_msg("Failed to set broker: %s", errstr);
	}
	init_rdkafka();
}

void test_zz_decoder_teardown(void *vstate) {
	struct zz_test_state *state = vstate;

	const struct n2k_decoder *decoder = state->listener->listener.decoder;
	void *decoder_opaque = state->listener->listener.decoder_opaque;

	state->listener->listener.join(&state->listener->listener);

	free_global_config();
}

/** Consume expected messages, and check that there is no more msgs
  @param rk rdkafka handler
  @param msgs Place to store messages
  @param expected_msgs Number of messages we are expecting
  */
static void n2k_consume_kafka_msgs(rd_kafka_t *rk,
				   rd_kafka_message_t *rkm[],
				   size_t expected_msgs) {
	assert_non_null(rk);
	assert_non_null(rkm);

	// Wait for pending produce() msgs
	for (;;) {
		const int outq_len = rd_kafka_outq_len(global_config.rk);
		rdlog(LOG_ERR, "%d rk outq len", outq_len);
		if (0 == outq_len) {
			break;
		}

		rd_kafka_poll(global_config.rk, 500);
	}

	size_t i = 0;
	while (i < expected_msgs) {
		rkm[i] = rd_kafka_consumer_poll(rk, 500);
		if (rkm[i] == NULL) {
			continue;
		}
		if (rkm[i]->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
			// Not really an error
			continue;
		} else if (rkm[i]->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
			fail_msg("Error consuming from kafka: %s",
				 rd_kafka_message_errstr(rkm[i]));
		}

		++i;
	}

	do {
		rd_kafka_message_t *eof_rmk = rd_kafka_consumer_poll(rk, 100);
		if (expected_msgs > 0) {
			// Check for end of queue
			if (NULL == eof_rmk) {
				continue;
			}
			assert_int_equal(eof_rmk->err,
					 RD_KAFKA_RESP_ERR__PARTITION_EOF);
			break;
		} else {
			// No new messages should have arrived
			assert_null(eof_rmk);
			break;
		}
	} while (1);
}

/** Template for zz_decoder test
	@param args Arguments like client_ip, topic, etc
	@param msgs Input messages
	@param msgs_len Length of msgs
	@param check_callback Array of functions that will be called
   with each
	session status. It is suppose to be the same length as msgs
   array.
	@param check_callback_opaque Opaque used in the second parameter
   of
	check_callback[iteration] call
	*/
void test_zz_decoder0(const json_t *listener_conf,
		      const json_t *decoder_conf,
		      const struct zz_http2k_params *params,
		      const struct message_in *msgs,
		      const check_callback_fn *check_callback,
		      size_t msgs_len,
		      const size_t *expected_kafka_msgs,
		      void *check_callback_opaque) {

	size_t i;
	// Need to copy because listener creation accept mutable json
	json_t *listener_conf_copy = json_deep_copy(listener_conf);

#define CONNECTION_POST_VALUE(t_key, t_val)                                    \
	{ .kind = MHD_HEADER_KIND, .key = t_key, .value = t_val, }

	// clang-format off
	const struct mock_MHD_connection_value conn_values[] = {
		// Add kong post parameters

		CONNECTION_POST_VALUE("X-Consumer-ID", params->consumer_uuid),
		CONNECTION_POST_VALUE("X-Consumer-Custom-ID", "not_important"),
		CONNECTION_POST_VALUE("X-Consumer-Username", "not_important"),
		CONNECTION_POST_VALUE("X-Credential-Username", "not_important"),
		CONNECTION_POST_VALUE("X-Anonymous-Consumer", "false"),
	};

#undef CONNECTION_POST_VALUE

	struct zz_test_state zz_state = {
		.mhd_connection = {
			.magic = MOCK_MHD_CONNECTION_MAGIC,
			.ret_client_addr.client_addr = (struct sockaddr *)
				&zz_state.mhd_connection.client_addr,
			.client_addr = {
				.sin_family = AF_INET,
				.sin_port = htons(3490),
				.sin_addr.s_addr = htonl(0x7f000001),
			},
			.connection_values = {
				.size = RD_ARRAYSIZE(conn_values),
				.values = conn_values

			}
		},
		.listener = create_http_listener(listener_conf_copy,
			                         &zz_decoder)
	};
	// clang-format on
	json_decref(listener_conf_copy);
	listener_conf_copy = NULL;

	static const char brokers[] = "kafka:9092";
	rd_kafka_t *rk = init_kafka_consumer(brokers, params->topic);

	test_zz_decoder_setup(&zz_state, decoder_conf);

	void *http_connection = NULL;

	post_handle(zz_state.listener,
		    (struct MHD_Connection *)&zz_state.mhd_connection,
		    params->uri,
		    "POST",
		    "1.1",
		    NULL,
		    (size_t[]){0},
		    &http_connection);

	for (i = 0; i < msgs_len; ++i) {
		rd_kafka_message_t *kafka_msgs[expected_kafka_msgs[i]];

		post_handle(zz_state.listener,
			    (struct MHD_Connection *)&zz_state.mhd_connection,
			    params->uri,
			    "POST",
			    "1.1",
			    msgs[i].msg,
			    (size_t[]){msgs[i].msg ? msgs[i].size : 0},
			    &http_connection);

		if (NULL == check_callback[i]) {
			// Nothing else to do
			continue;
		}

		// Wait for answers
		// @TODO don't assume that they are ordered!
		n2k_consume_kafka_msgs(rk, kafka_msgs, expected_kafka_msgs[i]);
		check_callback[i](kafka_msgs,
				  expected_kafka_msgs[i],
				  check_callback_opaque);
		size_t j;
		for (j = 0; j < expected_kafka_msgs[i]; ++j) {
			rd_kafka_message_destroy(kafka_msgs[j]);
		}
	}

	request_completed(zz_state.listener,
			  (struct MHD_Connection *)&zz_state.mhd_connection,
			  &http_connection,
			  MHD_REQUEST_TERMINATED_COMPLETED_OK);

	rd_kafka_consumer_close(rk);
	rd_kafka_destroy(rk);

	test_zz_decoder_teardown(&zz_state);
}

json_t *assert_json_loads(const char *json_txt) {
	json_error_t json_err;
	json_t *ret = json_loads(json_txt, 0, &json_err);
	if (NULL == ret) {
		fail_msg("Couldn't parse [%s] json: %s",
			 json_txt,
			 json_err.text);
	}

	return ret;
}

int __attribute__((unused))
__wrap_MHD_queue_response(struct MHD_Connection *connection,
			  unsigned int status_code,
			  struct MHD_Response *response) {
	return MHD_YES;
}

const union MHD_ConnectionInfo *__attribute__((unused))
__wrap_MHD_get_connection_info(struct MHD_Connection *vconnection,
			       enum MHD_ConnectionInfoType info_type,
			       ...) {
	struct mock_MHD_connection *connection = (void *)vconnection;
	assert_true(connection->magic == MOCK_MHD_CONNECTION_MAGIC);
	assert_int_equal(info_type, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
	return &connection->ret_client_addr;
}

int __attribute__((unused))
__wrap_MHD_get_connection_values(struct MHD_Connection *vconnection,
				 enum MHD_ValueKind kind,
				 MHD_KeyValueIterator iterator,
				 void *iterator_cls) {
	assert_int_equal(kind, MHD_HEADER_KIND);
	const struct mock_MHD_connection *connection = (void *)vconnection;
	size_t i;
	int count_rc = 0;

	for (i = 0; i < connection->connection_values.size; ++i) {
		if (connection->connection_values.values[i].kind != kind) {
			continue;
		}

		const int it_rc =
				iterator ? iterator(iterator_cls,
						    kind,
						    connection->connection_values
								    .values[i]
								    .key,
						    connection->connection_values
								    .values[i]
								    .value)
					 : MHD_YES;

		if (MHD_YES == it_rc) {
			count_rc++;
		}
	}

	return count_rc;
}
