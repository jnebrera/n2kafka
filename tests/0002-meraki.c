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

#include "n2k_kafka_tests.h"

#include <curl/curl.h>
#include <jansson.h>
#include <librdkafka/rdkafka.h>

#include <librd/rdio.h>

#include <arpa/inet.h>

#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cmocka.h>

#define n2k_topic_template "n2k_topic_XXXXXX"

static int test_tcp_socket() {
	const int s = socket(AF_INET, SOCK_STREAM, 0);
	assert_return_code(s, errno);
	return s;
}

static int create_test_localhost_tcp_socket0(
		uint16_t port,
		int (*callback)(int socket,
				const struct sockaddr *address,
				socklen_t address_len)) {
	const int s = test_tcp_socket();

	const struct sockaddr_in addr = {
			.sin_family = AF_INET,
			.sin_port = htons(port),
			.sin_addr.s_addr = inet_addr("127.0.0.1"),
	};
	const int connect_rc = callback(
			s, (const struct sockaddr *)&addr, sizeof(addr));
	assert_return_code(connect_rc, errno);

	return s;
}

static uint16_t random_port() {
	struct sockaddr_in name;
	const int s = create_test_localhost_tcp_socket0(0, bind);
	const int getsockname_rc = getsockname(s,
					       (struct sockaddr *)&name,
					       &(socklen_t){sizeof(name)});
	assert_return_code(getsockname_rc, errno);
	close(s); // No process should claim this port in CLOSE_WAIT time
	return name.sin_port;
}

/// Creates an file-based unique random name based on template
static int assert_mkstemp(char *template) {
	assert_non_null(template);
	const int mktmp_fd = mkstemp(template);
	assert_return_code(mktmp_fd, errno);
	return mktmp_fd;
}

static void random_topic(char *template) {
	const int topic_fd = assert_mkstemp(template);
	close(topic_fd);
}

struct test_listener {
	const char *decoder;
	const char *proto;
	int num_threads;
	uint16_t port;
};

static json_t *meraki_base_listener(struct test_listener *args) {
	static const size_t json_pack_flags = 0;
	json_error_t jerr;
	args->proto = args->proto ?: "http";
	args->port = args->port ?: random_port();
	args->num_threads = args->num_threads ?: 2;
	json_t *ret = json_pack_ex(&jerr,
				   json_pack_flags,
				   "{ss,si,ss,ss,si}",
				   "proto",
				   args->proto,
				   "port",
				   args->port,
				   "decode_as",
				   args->decoder,
				   "mode",
				   "epoll",
				   "num_threads",
				   args->num_threads);
	if (NULL == ret) {
		fail_msg("%s", jerr.text);
	}

	return ret;
}

static void meraki_base_config(char *config_filepath_template,
			       struct test_listener *t_listeners,
			       const char *default_topic) {
	static const size_t json_dumpfd_flags = 0;
	static const size_t json_pack_flags = 0;
	json_error_t jerr;
	json_t *listeners_array = json_array(), *secrets_array = json_array();
	assert_non_null(listeners_array);
	assert_non_null(secrets_array);

	while (t_listeners->proto) {
		json_t *listener = meraki_base_listener(t_listeners);
		if (NULL == default_topic) {
			// Per listener topic
			char listener_topic[sizeof(n2k_topic_template)];
			memcpy(listener_topic,
			       n2k_topic_template,
			       sizeof(listener_topic));
			random_topic(listener_topic);

			json_t *jlistener_topic = json_string(listener_topic);
			assert_non_null(jlistener_topic);
			const int set_rc = json_object_set(
					listener, "topic", jlistener_topic);
			assert_int_equal(set_rc, 0);
		}

		const int append_rc = json_array_append_new(listeners_array,
							    listener);
		assert_int_equal(0, append_rc);
		t_listeners++;
	}

	json_t *config = json_pack_ex(&jerr,
				      json_pack_flags,
				      "{so,ss,ss,ss}",
				      "listeners",
				      listeners_array,
				      "brokers",
				      "kafka",
				      "rdkafka.socket.max.fails",
				      "3",
				      "rdkafka.socket.keepalive.enable",
				      "true");
	if (NULL == config) {
		fail_msg("%s", jerr.text);
	}

	if (default_topic) {
		json_t *jdefault_topic = json_string(default_topic);
		assert_non_null(jdefault_topic);
		json_object_set_new(config, "topic", jdefault_topic);
	}

	const int config_fd = assert_mkstemp(config_filepath_template);
	assert_return_code(config_fd, errno);
	const int dumpfd_rc = json_dumpfd(config, config_fd, json_dumpfd_flags);
	assert_return_code(dumpfd_rc, errno);
}

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

struct meraki_test_ctx {
	rd_kafka_t *rk;

	struct {
		CURL *curl;
	} http;
} ctx = {};

static size_t curl_recv(char *ptr, size_t size, size_t nmemb, void *userdata) {
	const char **pexpected_buffer = userdata;

	if (*pexpected_buffer) {
		const int strcmp_rc =
				strncmp(ptr, *pexpected_buffer, size * nmemb);
		assert_int_equal(strcmp_rc, 0);
		*pexpected_buffer += size * nmemb;
	}

	return size * nmemb;
}

static void send_curl_message(const struct test_messages *msg) {
	curl_easy_reset(ctx.http.curl);

	curl_easy_setopt(ctx.http.curl, CURLOPT_URL, msg->uri);
	if (msg->send_msg) {
		curl_easy_setopt(ctx.http.curl,
				 CURLOPT_POSTFIELDS,
				 msg->send_msg);
	}

	const CURLcode set_writefunction_rc = curl_easy_setopt(
			ctx.http.curl, CURLOPT_WRITEFUNCTION, curl_recv);
	if (CURLE_OK != set_writefunction_rc) {
		fail_msg("Error setting curl recv function: %s",
			 curl_easy_strerror(set_writefunction_rc));
	}
	const char *expected_response_cursor = msg->expected_http_response;
	const CURLcode set_write_data_rc =
			curl_easy_setopt(ctx.http.curl,
					 CURLOPT_WRITEDATA,
					 &expected_response_cursor);
	if (set_write_data_rc != CURLE_OK) {
		fail_msg("%s", curl_easy_strerror(set_write_data_rc));
	}

	const CURLcode res = curl_easy_perform(ctx.http.curl);
	assert_int_equal(res, 0);

	long http_code = 0;
	curl_easy_getinfo(ctx.http.curl, CURLINFO_RESPONSE_CODE, &http_code);
	assert_int_equal(http_code, msg->http_response_code);
}

/// Extract topics we will have to read from
static rd_kafka_topic_partition_list_t *
test_toppar_list(const struct test_iteration *iteration) {
	rd_kafka_topic_partition_list_t *ret =
			rd_kafka_topic_partition_list_new(2);
	assert_non_null(ret);

	for (; iteration->test_messages; iteration++) {
		for (struct test_messages *test_messages =
				     iteration->test_messages;
		     test_messages->send_msg;
		     test_messages++) {

			const char *topic = test_messages->topic;
			if (NULL !=
			    rd_kafka_topic_partition_list_find(ret, topic, 0)) {
				continue;
			}

			rd_kafka_topic_partition_list_add(ret, topic, 0);
		}
	}

	return ret;
}

struct test_child {
	FILE *out;
	pid_t pid;
};

static struct test_child n2k_fork(const char *config_file_path) {
	struct test_child ret;

	enum { CHILD_READ_FD, CHILD_WRITE_FD };
	int fd[2];
	const int pipe_rc = pipe2(fd, O_NONBLOCK);
	assert_return_code(pipe_rc, errno);
	pipe(fd);

	ret.pid = fork();
	assert_return_code(ret.pid, errno);
	close(fd[ret.pid ? CHILD_WRITE_FD : CHILD_READ_FD]);

	if (0 == ret.pid) {
		char *args[] = {strdup("./n2kafka"),
				strdup(config_file_path),
				NULL};
		const int dup_rc = dup2(fd[CHILD_WRITE_FD], 1);
		assert_return_code(dup_rc, errno);
		const int rc = execvp(args[0], args);
		assert_return_code(rc, errno);
		return ret;
	} else {
		ret.out = fdopen(fd[CHILD_READ_FD], "r");
	}

	return ret;
}

static bool log_match_initialized(char *buf, const char *proto, uint16_t port) {
	enum { LOG_PARSE_DATE, LOG_PARSE_FUN, LOG_PARSE_ADDR, LOG_PARSE_MSG };
	char *cursor = buf;
	uint16_t read_port;
	char read_proto[sizeof("HTTP")];

	for (size_t i = 0; i < LOG_PARSE_MSG; i++) {
		cursor = strchr(cursor + 1, '|');
		if (NULL == cursor) {
			return false;
		}
	}

	const int scanf_rc =
			sscanf(cursor + 1,
			       " Creating new %5s listener on port %" SCNu16,
			       read_proto,
			       &read_port);
	assert_return_code(scanf_rc, errno);
	return (scanf_rc == 2 && 0 == strcmp(proto, read_proto) &&
		port == read_port);
}

static void wait_for_initialization(struct test_child *n2k,
				    const char *proto,
				    uint16_t port) {
	int fd = fileno(n2k->out);
	while (true) {
		char buf[256];

		static const int timeout_ms = 5000;
		const int poll_rc = rd_io_poll_single(fd, POLLIN, timeout_ms);
		if (poll_rc == EAGAIN) {
			fail_msg("Error waiting for n2kafka output");
		}

		char *fgets_rc = fgets(buf, sizeof(buf), n2k->out);
		if (NULL == fgets_rc) {
			assert_return_code(-1, errno);
		}

		if (log_match_initialized(buf, proto, port)) {
			break;
		}
	}
}

static bool
bool_array_all(const bool *array, size_t array_len, bool test_value) {
	for (size_t i = 0; i < array_len; ++i) {
		if (array[i] != test_value) {
			return false;
		}
	}

	return true;
}

static void consume_kafka_messages(rd_kafka_t *rk,
				   const char *expected_topic,
				   const char **expected_kafka_messages,
				   rd_kafka_topic_partition_list_t *topic_list,
				   bool *eof_reached) {

	if (NULL == *expected_kafka_messages) {
		// We are only expecting for EOF
		if (bool_array_all(eof_reached,
				   (size_t)topic_list->cnt,
				   true)) {
			return;
		}
	}

	while (true) {
		rd_kafka_message_t *rkmessage =
				rd_kafka_consumer_poll(rk, 60000);
		if (NULL == rkmessage) {
			fail_msg("Timeout consuming with kafka");
		}

		const char *topic_name = rd_kafka_topic_name(rkmessage->rkt);

		if (RD_KAFKA_RESP_ERR__PARTITION_EOF == rkmessage->err) {
			rd_kafka_topic_partition_t *eof =
					rd_kafka_topic_partition_list_find(
							topic_list,
							topic_name,
							rkmessage->partition);
			assert_non_null(eof);
			eof_reached[eof - topic_list->elems] = true;

			if (bool_array_all(eof_reached,
					   (size_t)topic_list->cnt,
					   true)) {
				return;
			}

		} else if (0 != rkmessage->err) {
			fail_msg("Error consuming from topic %s: %s",
				 topic_name,
				 rd_kafka_message_errstr(rkmessage));
		} else if ((expected_topic &&
			    strcmp(topic_name, expected_topic)) ||
			   NULL == *expected_kafka_messages) {
			expected_topic = expected_topic ?: "(null)";
			fail_msg("Unexpected message received from topic %s, "
				 "expected %s",
				 topic_name,
				 expected_topic);
		} else {
			assert_int_equal(
					0,
					strncmp(rkmessage->payload,
						*expected_kafka_messages,
						strlen(*expected_kafka_messages)));
			expected_kafka_messages++;
		}

		rd_kafka_message_destroy(rkmessage);
	}
}

static void reach_eof(rd_kafka_t *rk,
		      rd_kafka_topic_partition_list_t *topic_list,
		      bool *eof_reached) {
	const char *expected_topic = NULL;
	const char **expected_kafka_messages = (const char *[]){NULL};

	consume_kafka_messages(rk,
			       expected_topic,
			       expected_kafka_messages,
			       topic_list,
			       eof_reached);
}

#define asprintf(fmt...)                                                       \
	({                                                                     \
		struct {                                                       \
			char *ptr;                                             \
			int len;                                               \
		} asprintf;                                                    \
		asprintf.len = snprintf(NULL, 0, fmt) + 1;                     \
		assert_return_code(asprintf.len, errno);                       \
		asprintf.ptr = alloca((size_t)asprintf.len);                   \
		snprintf(asprintf.ptr, (size_t)asprintf.len, fmt);             \
		asprintf.ptr;                                                  \
	})

static void test_meraki(void **state) {
	char config_file_path[] = "n2k_config_XXXXXX";
	struct test_iteration *iterations = *state;
	assert_true(iterations->magic = TEST_ITERATION_MAGIC);

	/// @TODO reload
	meraki_base_config(config_file_path,
			   iterations->listeners,
			   iterations->default_topic);

	rd_kafka_topic_partition_list_t *topic_list =
			test_toppar_list(iterations);
	rd_kafka_resp_err_t assign_err = rd_kafka_assign(ctx.rk, topic_list);
	if (assign_err != RD_KAFKA_RESP_ERR_NO_ERROR) {
		fail_msg("Error assigning partitions: %s",
			 rd_kafka_err2str(assign_err));
	}

	struct test_child child = n2k_fork(config_file_path);
	/// @TODO wait for all listeners
	wait_for_initialization(&child, "HTTP", iterations->listeners[0].port);

	bool eof_reached[topic_list->cnt];
	for (size_t i = 0; i < sizeof(eof_reached); ++i) {
		eof_reached[i] = false;
	}
	reach_eof(ctx.rk, topic_list, eof_reached);

	for (; iterations->test_messages; iterations++) {
		for (struct test_messages *test_messages =
				     iterations->test_messages;
		     test_messages->uri;
		     test_messages++) {

			const size_t listener_idx = test_messages->listener_idx;
			const uint16_t listener_port =
					iterations->listeners[listener_idx]
							.port;

			test_messages->uri = asprintf("http://"
						      "localhost:%" PRIu16 "%s",
						      listener_port,
						      test_messages->uri);

			send_curl_message(test_messages);
			consume_kafka_messages(
					ctx.rk,
					test_messages->topic,
					test_messages->expected_kafka_messages,
					topic_list,
					eof_reached);

			// No more messages sent
			reach_eof(ctx.rk, topic_list, eof_reached);
		}
	}

	// Stop n2kafka
	kill(child.pid, SIGINT);
	const int wait_rc = wait(NULL);
	assert_return_code(wait_rc, errno);
}

static int meraki_group_setup(void **state) {
	(void)state;
	curl_global_init(CURL_GLOBAL_ALL);
	ctx.http.curl = curl_easy_init();

	ctx.rk = init_kafka_consumer("kafka:9092");
	return 0;
}

static int meraki_group_teardown(void **state) {
	(void)state;

	rd_kafka_consumer_close(ctx.rk);
	rd_kafka_destroy(ctx.rk);

	curl_easy_cleanup(ctx.http.curl);
	curl_global_cleanup();

	return 0;
}

int main() {
	char test_random_topic[] = n2k_topic_template;
	random_topic(test_random_topic);
	const char test_msg[] = "{\"test1\":1}";

	// clang-format off
	struct test_iteration test1[] = {
	{
		.magic = TEST_ITERATION_MAGIC,


		.listeners = (struct test_listener[]){
			{.proto = "http", .decoder = "meraki"},
			{},
		},
		.default_topic = test_random_topic,
		.test_messages = (struct test_messages[]){
			{
				.send_msg = test_msg,
				.uri = "/",
				.http_response_code = 200,
				.topic = test_random_topic,
				.listener_idx = 0,
				.expected_kafka_messages =
					(const char *[]){test_msg, NULL},
			}, {
				.uri = "/v1/meraki/myowntestvalidator",
				.http_response_code = 200,
				.topic = test_random_topic,
				.listener_idx = 0,
				.expected_http_response = "myowntestvalidator",
				.expected_kafka_messages =
					(const char *[]){NULL},
			},
			{}
		}
	},
	{}};
	// clang-format on

#define meraki_test(t_state) cmocka_unit_test_prestate(test_meraki, t_state)

	const struct CMUnitTest tests[] = {
			meraki_test(&test1),
	};
	return cmocka_run_group_tests(
			tests, meraki_group_setup, meraki_group_teardown);
}
