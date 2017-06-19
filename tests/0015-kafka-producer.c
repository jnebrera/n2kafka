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

#include "assertion_handler.c"
#include "n2k_kafka_tests.h"

#include <curl/curl.h>
#include <librdkafka/rdkafka.h>

#include <arpa/inet.h>

#include <fcntl.h>
#include <netinet/tcp.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cmocka.h>

#define SENT_MESSAGE_HTTP_1 "{\"message\": \"Hello world HTTP\"}"
#define SENT_MESSAGE_HTTP_2 "{\"message\": \"Sayounara baby HTTP\"}"
#define SENT_MESSAGE_TCP_1 "{\"message\": \"Hello world TCP\"}"
#define SENT_MESSAGE_TCP_2 "{\"message\": \"Sayounara baby TCP\"}"
#define VALID_URL "http://localhost:2057"
#define TCP_HOST "127.0.0.1"
#define TCP_PORT 2056
#define TCP_MESSAGES_DELAY 5

static void send_curl_messages(struct assertion_handler_s *assertion_handler,
			       void *opaque) {
	(void)opaque;
	curl_global_init(CURL_GLOBAL_ALL);

	// Init curl
	CURL *curl = curl_easy_init();
	assert_non_null(curl);

	const char *messages[] = {
			SENT_MESSAGE_HTTP_1, SENT_MESSAGE_HTTP_2,
	};

	size_t i;
	for (i = 0; i < RD_ARRAYSIZE(messages); ++i) {
		assertion_handler_push_assertion(assertion_handler,
						 messages[i]);

		// Send message via curl
		curl_easy_setopt(curl, CURLOPT_URL, VALID_URL);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, messages[i]);
		const CURLcode res = curl_easy_perform(curl);
		assert_int_equal(res, 0);

		long http_code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
		assert_int_equal(http_code, 200);
	}

	curl_easy_cleanup(curl);
	curl_global_cleanup();
}

static void test_send_message_base(
		const char *config_file,
		void (*send_messages_cb)(
				struct assertion_handler_s *assertion_handler,
				void *opaque),
		void *opaque) {
	rd_kafka_t *rk = init_kafka_consumer("kafka:9092", "rb_flow");
	struct assertion_handler_s assertion_handler = {};
	assertion_handler_new(&assertion_handler);

	// Fork n2kafka
	const pid_t pID = fork();

	if (pID == 0) {
		const int exec_rc = execlp(
				"./n2kafka", "n2kafka", config_file, (char *)0);
		assert_return_code(exec_rc, errno);

	} else if (pID < 0) {
		assert_return_code(pID, errno);
	}

	// Wait for n2kafka to initialize
	sleep(1);

	send_messages_cb(&assertion_handler, opaque);

	// Try to read the messages from kafka and check them agains assertions
	while (!assertion_handler_empty(&assertion_handler)) {
		rd_kafka_message_t *rkmessage =
				rd_kafka_consumer_poll(rk, 1000);
		if (rkmessage == NULL) {
			// still waiting to the next message
			continue;
		}

		if (RD_KAFKA_RESP_ERR__PARTITION_EOF == rkmessage->err) {
			// Not a real error, not a real message
		} else if (0 != rkmessage->err) {
			fail_msg("Error consuming from topic %s: %s",
				 rd_kafka_topic_name(rkmessage->rkt),
				 rd_kafka_message_errstr(rkmessage));
		} else {
			assertion_handler_assert(&assertion_handler,
						 rkmessage->payload,
						 rkmessage->len);
		}

		rd_kafka_message_destroy(rkmessage);
	}

	// Close consumer
	rd_kafka_consumer_close(rk);

	// Clean consumer
	rd_kafka_destroy(rk);

	// Stop n2kafka
	kill(pID, SIGINT);
	const int wait_rc = wait(NULL);
	assert_return_code(wait_rc, errno);

	// Assert no pending messages
	assertion_handler_empty(&assertion_handler);
}

/**
 * Send a message using curl and expect to receive it via kafka
 */
static void test_send_message_http() {
	test_send_message_base("configs_example/n2kafka_tests_http.json",
			       send_curl_messages,
			       NULL);
}

static void
send_tcp_messages(struct assertion_handler_s *assertion_handler, void *opaque) {
	(void)opaque;
	struct sockaddr_in server = {
			.sin_addr.s_addr = inet_addr(TCP_HOST),
			.sin_family = AF_INET,
			.sin_port = htons(TCP_PORT),
	};

	const int sock = socket(AF_INET, SOCK_STREAM, 0);
	assert_return_code(sock, errno);

	// Connect to remote server
	const int connect_rc = connect(
			sock, (struct sockaddr *)&server, sizeof(server));
	assert_return_code(connect_rc, errno);

	// Send some data
	static const char *tcp_msgs[] = {SENT_MESSAGE_TCP_1,
					 SENT_MESSAGE_TCP_2};

	size_t i = 0;
	for (i = 0; i < RD_ARRAYSIZE(tcp_msgs); ++i) {
		const int send_rc =
				send(sock, tcp_msgs[i], strlen(tcp_msgs[i]), 0);
		assert_return_code(send_rc, errno);

		assertion_handler_push_assertion(assertion_handler,
						 tcp_msgs[i]);

		if (i < RD_ARRAYSIZE(tcp_msgs)) {
			// no wait causes TCP stream messages merging
			sleep(TCP_MESSAGES_DELAY);
		}
	}

	close(sock);
}

/**
 * Send a message using a TCP socket and expect to receive it via kafka
 */
static void test_send_message_tcp() {
	test_send_message_base("configs_example/n2kafka_tests_tcp.json",
			       send_tcp_messages,
			       NULL);
}

int main() {
	const struct CMUnitTest tests[] = {
			cmocka_unit_test(test_send_message_http),
			cmocka_unit_test(test_send_message_tcp)};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
