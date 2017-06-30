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

#include <alloca.h>
#include <fcntl.h>
#include <netinet/in.h>

static const char ZZ_LOCK_FILE[] = "n2kt_listener.lck";
static rd_kafka_t *rk_consumer = NULL;

int test_zz_decoder_group_tests_setup(void **state) {
	(void)state;
	init_global_config();

	global_config.brokers = strdup("kafka:9092");

	struct {
		const char *key, *value;
	} rk_props[] = {{
					.key = "metadata.broker.list",
					.value = global_config.brokers,
			},
			{
					// reduce produce latency
					.key = "queue.buffering.max.ms",
					.value = "1",
			}};

	size_t i;
	for (i = 0; i < RD_ARRAYSIZE(rk_props); ++i) {
		char errstr[256];

		const rd_kafka_conf_res_t conf_set_rc =
				rd_kafka_conf_set(global_config.kafka_conf,
						  rk_props[i].key,
						  rk_props[i].value,
						  errstr,
						  sizeof(errstr));
		if (conf_set_rc != RD_KAFKA_CONF_OK) {
			fail_msg("Failed to set %s to [%s]: %s",
				 rk_props[i].key,
				 rk_props[i].value,
				 errstr);
		}
	}

	init_rdkafka();
	rk_consumer = init_kafka_consumer(global_config.brokers);

	return 0;
}

int test_zz_decoder_group_tests_teardown(void **state) {
	(void)state;
	rd_kafka_consumer_close(rk_consumer);
	rd_kafka_destroy(rk_consumer);

	free_global_config();
	return 0;
}

struct mock_MHD_connection_value {
	enum MHD_ValueKind kind;
	const char *key, *value;
};

struct mock_MHD_connection {
#define MOCK_MHD_CONNECTION_MAGIC 0x0CDC0310A1CCDC031
	uint64_t magic;
	union MHD_ConnectionInfo ret_client_addr;
	struct sockaddr_in client_addr;

	struct {
		size_t size;
		const struct mock_MHD_connection_value *values;
	} connection_values;
};

struct zz_test_state {
	struct mock_MHD_connection mhd_connection;
	struct http_listener *listener;
};

static void
test_zz_decoder_setup(struct zz_test_state *zz_state,
		      const struct mock_MHD_connection_value *conn_values,
		      size_t conn_values_size,
		      const json_t *listener_conf) {
	// clang-format off
	*zz_state = (struct zz_test_state){
		.mhd_connection = {
			.magic = MOCK_MHD_CONNECTION_MAGIC,
			.ret_client_addr.client_addr = (struct sockaddr *)
				&zz_state->mhd_connection.client_addr,
			.client_addr = {
				.sin_family = AF_INET,
				.sin_port = htons(3490),
				.sin_addr.s_addr = htonl(0x7f000001),
			},
			.connection_values = {
				.values = conn_values,
				.size = conn_values_size,

			}
		},
		.listener = (void *)create_http_listener(listener_conf,
			                                     &zz_decoder),
	};
	// clang-format on
}

static void test_zz_decoder_teardown(struct zz_test_state *state) {
	state->listener->listener.join(&state->listener->listener);
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
			rd_kafka_message_destroy(rkm[i]);
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
			rd_kafka_message_destroy(eof_rmk);
			break;
		} else {
			// No new messages should have arrived
			assert_null(eof_rmk);
			break;
		}
	} while (1);
}

/** Stack-allocated snprintf
  @param fmt String format
  @param ... format attributes
  @return Expected topic in stack-allocated buffer
  */
#define alloca_sprintf(fmt...)                                                 \
	({                                                                     \
		char *ret = NULL;                                              \
		int size = 0, pass = 0;                                        \
		for (pass = 0; pass < 2; pass++) {                             \
			size = snprintf(ret, (size_t)size, fmt);               \
			assert_return_code(size, errno);                       \
			size++;                                                \
			if (NULL == ret) {                                     \
				ret = alloca((size_t)size);                    \
			}                                                      \
		}                                                              \
		ret;                                                           \
	})

/** Print expected topic in stack allocated buffer
  @param t_uuid Consumer uuid
  @param t_topic Consumer topic
  @return stack-allocated topic buffer
  */
#define expected_topic(t_uuid, t_topic) alloca_sprintf("%s_%s", t_uuid, t_topic)
#define zz_url(t_uuid, t_topic) alloca_sprintf("/v1/data/%s", t_topic)

void test_zz_decoder(void **vparams) {
	struct zz_http2k_params *params = *vparams;

	//
	// Prepare default arguments
	//
	static const char zz_topic_template[] = "n2ktXXXXXX";

	char topic[sizeof(zz_topic_template)];
	if (NULL == params->topic) {
		// Default
		strcpy(topic, zz_topic_template);
		random_topic_name(topic);
		params->topic = topic;
	}

	if (NULL == params->out_topic) {
		// Default
		params->out_topic =
				params->consumer_uuid
						? expected_topic(params->consumer_uuid,
								 params->topic)
						: params->topic;
	}

	if (NULL == params->uri) {
		params->uri = zz_url(consumer_uuid, topic);
	}

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
// clang-format on

#undef CONNECTION_POST_VALUE

	//
	// ACTUAL TEST
	//

	struct zz_test_state zz_state;
	test_zz_decoder_setup(&zz_state,
			      conn_values,
			      RD_ARRAYSIZE(conn_values),
			      params->listener_conf);

	set_rdkafka_consumer_topics(rk_consumer, params->out_topic);
	void *http_connection = NULL;

	post_handle(zz_state.listener,
		    (struct MHD_Connection *)&zz_state.mhd_connection,
		    params->uri,
		    "POST",
		    "1.1",
		    NULL,
		    (size_t[]){0},
		    &http_connection);

	size_t i;
	for (i = 0; i < params->msgs.num; ++i) {
		const struct message_in *msg_i = &params->msgs.msg[i];
		const size_t expected_kafka_msgs =
				msg_i->expected_kafka_messages;
		rd_kafka_message_t *kafka_msgs[expected_kafka_msgs];

		post_handle(zz_state.listener,
			    (struct MHD_Connection *)&zz_state.mhd_connection,
			    params->uri,
			    "POST",
			    "1.1",
			    msg_i->msg,
			    (size_t[]){msg_i->msg ? msg_i->size : 0},
			    &http_connection);

		if (NULL == msg_i->check_callback_fn) {
			// Nothing else to do
			continue;
		}

		// Wait for answers
		// @TODO don't assume that they are ordered!
		n2k_consume_kafka_msgs(
				rk_consumer, kafka_msgs, expected_kafka_msgs);
		msg_i->check_callback_fn(kafka_msgs,
					 msg_i->expected_kafka_messages,
					 msg_i->check_cb_opaque);
		size_t j;
		for (j = 0; j < expected_kafka_msgs; ++j) {
			rd_kafka_message_destroy(kafka_msgs[j]);
		}
	}

	request_completed(zz_state.listener,
			  (struct MHD_Connection *)&zz_state.mhd_connection,
			  &http_connection,
			  MHD_REQUEST_TERMINATED_COMPLETED_OK);

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

int __wrap_MHD_queue_response(struct MHD_Connection *connection,
			      unsigned int status_code,
			      struct MHD_Response *response);
int __wrap_MHD_queue_response(struct MHD_Connection *connection,
			      unsigned int status_code,
			      struct MHD_Response *response) {
	(void)connection;
	(void)status_code;
	(void)response;
	return MHD_YES;
}

const union MHD_ConnectionInfo *
__wrap_MHD_get_connection_info(struct MHD_Connection *vconnection,
			       enum MHD_ConnectionInfoType info_type,
			       ...);
const union MHD_ConnectionInfo *
__wrap_MHD_get_connection_info(struct MHD_Connection *vconnection,
			       enum MHD_ConnectionInfoType info_type,
			       ...) {
	struct mock_MHD_connection *connection = (void *)vconnection;
	assert_true(connection->magic == MOCK_MHD_CONNECTION_MAGIC);
	assert_int_equal(info_type, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
	return &connection->ret_client_addr;
}

int __wrap_MHD_get_connection_values(struct MHD_Connection *vconnection,
				     enum MHD_ValueKind kind,
				     MHD_KeyValueIterator iterator,
				     void *iterator_cls);
int __wrap_MHD_get_connection_values(struct MHD_Connection *vconnection,
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
