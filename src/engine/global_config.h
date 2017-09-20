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

#pragma once

#include "config.h"

#include "decoder/decoder_api.h"
#include "decoder/meraki/meraki.h"
#include "decoder/mse/rb_mse.h"
#include "decoder/zz_http2k/zz_http2k_decoder.h"
#include "util/in_addr_list.h"
#include "util/kafka.h"
#include "util/pair.h"
#include "util/rb_timer.h"

#include <librdkafka/rdkafka.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/queue.h>

enum {
	/**
	    Tells if the listener support straming API, instead of wait to all
	   data
	    to be available
	*/
	DECODER_F_SUPPORT_STREAMING = 0x01,
};

typedef LIST_HEAD(, listener) listener_list;

struct n2kafka_config {
	/// Path to reload
	char *config_path;

	char *format;

	char *topic;
	char *brokers;
	char *n2kafka_id;

	rd_kafka_conf_t *kafka_conf;
	rd_kafka_topic_conf_t *kafka_topic_conf;
	rd_kafka_t *rk;

	in_addr_list_t *blacklist;

	/// List of global timers
	rb_timers_list_t timers;

	char *response;
	int response_len;

	listener_list listeners;

	bool debug;
};

extern struct n2kafka_config global_config;

static inline bool only_stdout_output() {
	return global_config.debug && !global_config.brokers;
}

void init_global_config();

void parse_config(const char *config_file_path);

void reload_config(struct n2kafka_config *config);

/** Register a new timer to call from this decoder
  @param interval Interval to call callback
  @param cb Callback
  @param ctx Context to call callback
  @note Callback will be called from another thread than decoder.
  */
rb_timer_t *decoder_register_timer(const struct itimerspec *interval,
				   void (*cb)(void *),
				   void *ctx);

/** Unregister a registered timer
  @param timer timer to unregister
  */
void decoder_deregister_timer(rb_timer_t *timer);

/** Change a timer interval
  @param timer timer to change interval
  @param ts New timer interval
  @return 0 if success. !0 in other case.
  */
int decoder_timer_set_interval0(struct rb_timer *timer,
				int flags,
				const struct itimerspec *ts);

#define decoder_timer_set_interval(timer, timerspec)                           \
	decoder_timer_set_interval0(timer, 0, timerspec)

/// @TODO use SIGEV_THREAD and delete this function!!
void execute_global_timers();

void free_global_config();
