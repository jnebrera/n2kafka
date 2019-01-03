/*
**
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
** Copyright (C) 2018-2019, Wizzie S.L.
** Author: Eugenio Perez <eupm90@gmail.com>
**
** This file is part of n2kafka.
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

#include <librdkafka/rdkafka.h>

#include <time.h>

struct topic_s;

void topic_decref(struct topic_s *topic);

/** Topics database.
  @note All functions are thread safe, and you can keep a topic even after
   call topics_db_done */
struct topics_db;

/** It creates a new database */
struct topics_db *topics_db_new();

/** Get a topic from database.
	@param db Database
	@param topic Topic name to search
	@param now current timestamp for old topics removal
	@return Associated topic. Need to decref returned value when finish. */
struct topic_s *
topics_db_get_topic(struct topics_db *db, const char *topic, const time_t now);

/** Extract rdkafka topic from topic handler
	@param topic topic handler
	@return rdkafka usable topic */
rd_kafka_topic_t *topics_db_get_rdkafka_topic(struct topic_s *topic);

/** Get topic handler's topic name
	@param topic Topic handler
	@return Name of topic
	*/
const char *topics_db_get_topic_name(const struct topic_s *topic);

/** Destroy a topics db.
	You can keep using topics extracted from it, but you can't search for
   more topics
	*/
void topics_db_done(struct topics_db *topics_db);
