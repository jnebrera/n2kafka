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
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include <librdkafka/rdkafka.h>

#define SENT_MESSAGE_HTTP_1 "{\"message\": \"Hello world HTTP\"}"
#define SENT_MESSAGE_HTTP_2 "{\"message\": \"Sayounara baby HTTP\"}"
#define SENT_MESSAGE_TCP_1 "{\"message\": \"Hello world TCP\"}"
#define SENT_MESSAGE_TCP_2 "{\"message\": \"Sayounara baby TCP\"}"
#define N_MESSAGES_EXPECTED 10
#define VALID_URL "http://localhost:2057"
#define TCP_HOST "127.0.0.1"
#define TCP_PORT 2056
#define TCP_MESSAGES_DELAY 5

/**
 * Send a message using curl and expect to receive the enriched message via
 * kafka
 */
static void test_send_message_http() {
	CURL *curl;
	CURLcode res;
	long http_code = 0;
	rd_kafka_t *rk = init_kafka_consumer("kafka:9092", "rb_flow");
	struct assertion_handler_s *assertion_handler = NULL;

	curl_global_init(CURL_GLOBAL_ALL);

	// Fork n2kafka
	pid_t pID = fork();

	if (pID == 0) {
		// Close stdin, stdout, stderr
		// close(0);
		// close(1);
		// close(2);
		// open("/dev/null", O_RDWR);
		// (void)(dup(0) + 1);
		// (void)(dup(0) + 1);

		execlp("./n2kafka",
		       "n2kafka",
		       "configs_example/n2kafka_tests_http.json",
		       (char *)0);
		printf("Error executing n2kafka\n");
		exit(1);

	} else if (pID < 0) {
		exit(1);
	}

	// Wait for n2kafka to initialize
	sleep(1);

	// Init curl
	curl = curl_easy_init();

	// Use the assertion_handler to proccess assertions asynchronously
	assertion_handler = assertion_handler_new();

	if (curl) {
		// Push an assertion to the assertion handler with the expected
		// message
		struct assertion_e *assertion = (struct assertion_e *)malloc(
				sizeof(struct assertion_e));
		assertion->str = strdup(SENT_MESSAGE_HTTP_1);
		assertion_handler_push_assertion(assertion_handler, assertion);

		struct assertion_e *assertion2 = (struct assertion_e *)malloc(
				sizeof(struct assertion_e));
		assertion2->str = strdup(SENT_MESSAGE_HTTP_2);
		assertion_handler_push_assertion(assertion_handler, assertion2);

		// Send message via curl
		curl_easy_setopt(curl, CURLOPT_URL, VALID_URL);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, SENT_MESSAGE_HTTP_1);
		res = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

		// Assertions
		assert_int_equal(res, 0);
		assert_true(http_code == 200);

		// Send message 2 via curl
		curl_easy_setopt(curl, CURLOPT_URL, VALID_URL);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, SENT_MESSAGE_HTTP_2);
		res = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

		// Assertions
		assert_int_equal(res, 0);
		assert_true(http_code == 200);

		curl_easy_cleanup(curl);
	}

	// Try to read the message from kafka and push it to the assertion
	// handler
	int i = 0;
	for (i = 0; i < N_MESSAGES_EXPECTED; i++) {
		rd_kafka_message_t *rkmessage;
		struct value_e *value;

		rkmessage = rd_kafka_consumer_poll(rk, 1000);
		if (rkmessage != NULL && rkmessage->len > 0) {
			value = (struct value_e *)malloc(
					sizeof(struct value_e));
			value->str = malloc((rkmessage->len + 1) *
					    sizeof(char));
			memmove(value->str, rkmessage->payload, rkmessage->len);
			value->str[rkmessage->len] = '\0';
			value->len = rkmessage->len;
			rd_kafka_message_destroy(rkmessage);
			assertion_handler_push_value(assertion_handler, value);
		}
	}

	// Close consumer
	rd_kafka_consumer_close(rk);
	sleep(1);

	// Clean consumer
	rd_kafka_destroy(rk);

	// Stop n2kafka
	kill(pID, SIGINT);
	curl_global_cleanup();

	// Assert pending messages
	assert_int_equal(assertion_handler_assert(assertion_handler), 0);
	assertion_handler_destroy(assertion_handler);
}

/**
 * Send a message using a TCP socket and expect to receive the enriched message
 * via kafka
 */
static void test_send_message_tcp() {
	int sock;
	struct sockaddr_in server;
	struct assertion_handler_s *assertion_handler = NULL;

	rd_kafka_t *rk = init_kafka_consumer("kafka:9092", "rb_flow");
	if (rk == NULL) {
		exit(1);
	}

	// Fork n2kafka
	pid_t pID = fork();

	if (pID == 0) {
		// Close stdin, stdout, stderr
		close(0);
		close(1);
		close(2);
		open("/dev/null", O_RDWR);
		(void)(dup(0) + 1);
		(void)(dup(0) + 1);

		execlp("./n2kafka",
		       "n2kafka",
		       "configs_example/n2kafka_tests_tcp.json",
		       (char *)0);
		printf("Error executing n2kafka\n");
		exit(1);

	} else if (pID < 0) {
		exit(1);
	}

	// Wait for n2kafka to initialize
	sleep(1);

	// Use the assertion_handler to proccess assertions asynchronously
	assertion_handler = assertion_handler_new();

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		perror("Could not create socket. Error");
		exit(1);
	}

	server.sin_addr.s_addr = inet_addr(TCP_HOST);
	server.sin_family = AF_INET;
	server.sin_port = htons(TCP_PORT);

	// Connect to remote server
	if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
		perror("Connection failed. Error");
		exit(1);
	}

	// Send some data
	if (send(sock, SENT_MESSAGE_TCP_1, strlen(SENT_MESSAGE_TCP_1), 0) < 0) {
		perror("Send failed. Error");
		exit(1);
	}

	sleep(TCP_MESSAGES_DELAY);

	// Send some data
	if (send(sock, SENT_MESSAGE_TCP_2, strlen(SENT_MESSAGE_TCP_2), 0) < 0) {
		perror("Send failed. Error");
		exit(1);
	}

	// Add assertions
	struct assertion_e *assertion = NULL;

	assertion = (struct assertion_e *)malloc(sizeof(struct assertion_e));
	assertion->str = strdup(SENT_MESSAGE_TCP_1);
	assertion_handler_push_assertion(assertion_handler, assertion);

	assertion = (struct assertion_e *)malloc(sizeof(struct assertion_e));
	assertion->str = strdup(SENT_MESSAGE_TCP_2);
	assertion_handler_push_assertion(assertion_handler, assertion);

	// Try to read the message from kafka and push it to the assertion
	// handler
	int i = 0;
	for (i = 0; i < N_MESSAGES_EXPECTED; i++) {
		rd_kafka_message_t *rkmessage;
		struct value_e *value;

		rkmessage = rd_kafka_consumer_poll(rk, 1000);
		if (rkmessage != NULL && rkmessage->len > 0) {
			value = (struct value_e *)malloc(
					sizeof(struct value_e));
			value->str = malloc((rkmessage->len + 1) *
					    sizeof(char));
			memmove(value->str, rkmessage->payload, rkmessage->len);
			value->str[rkmessage->len] = '\0';
			value->len = rkmessage->len;
			rd_kafka_message_destroy(rkmessage);
			assertion_handler_push_value(assertion_handler, value);
		}
	}

	// Close consumer
	rd_kafka_consumer_close(rk);
	sleep(1);

	// Clean consumer
	rd_kafka_destroy(rk);

	kill(pID, SIGINT);

	// Assert pending messages
	assert_int_equal(assertion_handler_assert(assertion_handler), 0);
	assertion_handler_destroy(assertion_handler);
	close(sock);
}

int main() {
	const struct CMUnitTest tests[] = {
			cmocka_unit_test(test_send_message_http),
			cmocka_unit_test(test_send_message_tcp)};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
