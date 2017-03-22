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

#include <librd/rdlog.h>

#include <assert.h>
#include <errno.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int init_zz_database(struct zz_database *db) {
	char errbuf[BUFSIZ];

	memset(db, 0, sizeof(*db));
	const int pthread_rc = pthread_rwlock_init(&db->rwlock, 0);

	if (pthread_rc != 0) {
		strerror_r(errno, errbuf, sizeof(errbuf));
		rdlog(LOG_ERR, "Can't start rwlock: %s", errbuf);
		return pthread_rc;
	}

	return 0;
}

void free_valid_zz_database(struct zz_database *db) {
	if (db) {
		if (db->topics_db) {
			topics_db_done(db->topics_db);
		}

		pthread_rwlock_destroy(&db->rwlock);
	}
}

struct topic_s *zz_http2k_database_get_topic(struct zz_database *db,
					     const char *topic,
					     const char *client_uuid) {
	assert(db);
	assert(client_uuid);

	pthread_rwlock_rdlock(&db->rwlock);
	struct topic_s *ret = topics_db_get_topic(db->topics_db, topic);
	pthread_rwlock_unlock(&db->rwlock);

	return ret;
}
