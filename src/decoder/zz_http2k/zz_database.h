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

#include <time.h>

struct zz_database {
	struct topics_db *topics_db; ///< Topics db
};

/** Initialized a zz_database
  @param db database to init
  @return 0 if success, !0 in other case
  */
int init_zz_database(struct zz_database *db);
void free_valid_zz_database(struct zz_database *db);

/**
	Get sensor enrichment and topic of an specific database.

	@param db Database to extract topic handler from
	@param topic Topic to search for
	@param now Current timestamp for old topic removal
	@return topic_handler Returned topic handler. Need to be freed with
	topic_decref. NULL in case of error.
	*/
struct topic_s *zz_http2k_database_get_topic(struct zz_database *db,
					     const char *topic,
					     const time_t now);
