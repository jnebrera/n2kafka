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

#include "zz_database.h"

#include "util/topic_database.h"

#include <assert.h>
#include <stddef.h>

int init_zz_database(struct zz_database *db) {
	db->topics_db = topics_db_new();
	return db->topics_db != NULL ? 0 : 1;
}

void free_valid_zz_database(struct zz_database *db) {
	if (db->topics_db) {
		topics_db_done(db->topics_db);
	}
}

struct topic_s *zz_http2k_database_get_topic(struct zz_database *db,
					     const char *topic,
					     const time_t now) {
	assert(db);
	assert(topic);

	struct topic_s *ret = topics_db_get_topic(db->topics_db, topic, now);

	return ret;
}
