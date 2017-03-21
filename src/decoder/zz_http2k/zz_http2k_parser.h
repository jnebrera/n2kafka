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

#pragma once

#include "zz_http2k_sensors_database.h"

#include <jansson.h>
#include <util/kafka_message_list.h>
#include <util/pair.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_parse.h>

/// @TODO many of the fields here could be a state machine
/// @TODO separate parsing <-> not parsing fields
/// @TODO could this be private?
struct zz_session {
	/// Output generator.
	yajl_gen gen;

	/// JSON handler
	yajl_handle handler;

	/// Sensor information.
	sensor_db_entry_t *sensor;

	/// Bookmark if we are skipping an object or array
	size_t object_array_parsing_stack;

	/// Per POST business.
	const char *client_ip, *sensor_uuid;

	/// Topid handler
	struct topic_s *topic_handler;

	struct {
		int valid;
	} message;

	/// Message list in this call to decode()
	rd_kafka_message_queue_t msg_queue;

	/// Skip next parsing value
	int skip_value;
};

struct zz_config;
struct zz_session *
new_zz_session(struct zz_config *zz_config, const keyval_list_t *msg_vars);

int gen_jansson_object(yajl_gen gen, json_t *enrichment_data);

void free_zz_session(struct zz_session *sess);
