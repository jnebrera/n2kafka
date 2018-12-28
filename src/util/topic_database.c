/*
**
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
** Copyright (C) 2018, Wizzie S.L.
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

#include "config.h"

#include "topic_database.h"

#include "engine/global_config.h"

#include "util/util.h"

#include <librdkafka/rdkafka.h>
#include <tommyds/tommyhash.h>
#include <tommyds/tommyhashdyn.h>
#include <tommyds/tommylist.h>
#include <tommyds/tommytypes.h>

#include <librd/rdlog.h>
#include <librd/rdmem.h>

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

static const uint64_t hash_init = 0x0123456789abcdefL;
static const double topic_live_time_s = 15 * 60;

typedef tommy_list topics_list;

#define topic_list_init(l) tommy_list_init(l)
#define topic_list_head(l) tommy_list_head(l)
#define topic_list_insert_tail(l, e)                                           \
	tommy_list_insert_tail(l, &(e)->list_node, e)
#define topic_list_remove(l, e) tommy_list_remove_existing(l, &(e)->list_node)

struct topic_s {
#ifndef NDEBUG
#define TOPIC_S_MAGIC 0x01CA1C01CA1C01CL
	uint64_t magic;
#endif
	rd_kafka_topic_t *rkt;
	const char *topic_name;
	uint64_t refcnt;
	time_t timestamp;

	tommy_node hashtable_node, list_node;
};

static void assert_topic_s(const struct topic_s *topic) {
#ifdef TOPIC_S_MAGIC
	assert(topic);
	assert(TOPIC_S_MAGIC == topic->magic);
#else
	(void)topic;
#endif
}

rd_kafka_topic_t *topics_db_get_rdkafka_topic(struct topic_s *topic) {
	return topic->rkt;
}

void topic_decref(struct topic_s *topic) {
	if (0 == ATOMIC_OP(sub, fetch, &topic->refcnt, 1)) {
		rd_kafka_topic_destroy(topic->rkt);
		free(topic);
	}
}

/// Auxiliary function for void
static void void_topic_decref(void *vtopic) {
	struct topic_s *topic = vtopic;
	assert_topic_s(topic);
	topic_decref(topic);
}

static int topics_cmp(const struct topic_s *topic, const char *topic_name) {
	assert(topic);
	assert(topic_name);

	return strcmp(topic->topic_name, topic_name);
}

static int void_topics_cmp(const void *arg, const void *obj) {
	assert(arg);
	assert(obj);
	const struct topic_s *tobj = obj;

	assert_topic_s(tobj);

	return topics_cmp(tobj, arg);
}

struct topics_db {
	pthread_mutex_t lock;    ///< Multi thread protection
	tommy_hashdyn hashtable; ///< Hash table
	topics_list lru;	 ///< Old purge
};

struct topics_db *topics_db_new() {
	struct topics_db *ret = calloc(1, sizeof(*ret));

	if (ret) {
		pthread_mutex_init(&ret->lock, NULL);
		tommy_hashdyn_init(&ret->hashtable);
		topic_list_init(&ret->lru);
	}

	return ret;
}

/** Create a new topic handler, 1 refcnt
  @param topic_name Name of the new topic
  @return New topic
  */
static struct topic_s *new_topic_s(const char *topic_name) {
	struct topic_s *ret = NULL;
	rd_kafka_topic_conf_t *rkt_conf =
			rd_kafka_topic_conf_dup(global_config.kafka_topic_conf);

	if (unlikely(!rkt_conf)) {
		rdlog(LOG_ERR, "Couldn't duplicate conf (OOM?)");
		return NULL;
	}

	rd_kafka_topic_t *rkt = rkt = rd_kafka_topic_new(
			global_config.rk, topic_name, rkt_conf);
	if (unlikely(NULL == rkt)) {
		rdlog(LOG_ERR,
		      "Can't create topic %s: %s",
		      topic_name,
		      gnu_strerror_r(errno));
		rd_kafka_topic_conf_destroy(rkt_conf);
		return NULL;
	}

	rd_calloc_struct(&ret,
			 sizeof(*ret),
			 -1,
			 topic_name,
			 &ret->topic_name,
			 RD_MEM_END_TOKEN);

	if (unlikely(!ret)) {
		rdlog(LOG_ERR, "Couldn't allocate topic (out of memory?)");
		goto alloc_err;
	}

#ifdef TOPIC_S_MAGIC
	ret->magic = TOPIC_S_MAGIC;
#endif
	ret->rkt = rkt;
	ret->refcnt = 1;

	return ret;

alloc_err:
	rd_kafka_topic_destroy(rkt);
	return NULL;
}

/** Check if a topic is older than a timestamp
  @param topic Topic to check
  @param age age limit
  @return 0 if it's newer, !0 if older
  */
static int node_older_than(const struct topic_s *topic, const time_t age) {
	return difftime(age, topic->timestamp) > 0;
}

/** Purge topics older than a timestamp.
  @param list Ordered topic LRU list
  @param age_limit Age to compare topics
  @warning this function is not thread safe!
  */
static void
purge_topics_older_than(struct topics_db *db, const time_t age_limit) {
	tommy_node *node = topic_list_head(&db->lru);

	while (node) {
		struct topic_s *topic = node->data;
		node = node->next;
		assert_topic_s(topic);

		if (node_older_than(topic, age_limit)) {
			tommy_hashdyn_remove_existing(&db->hashtable,
						      &topic->hashtable_node);
			tommy_list_remove_existing(&db->lru, &topic->list_node);
			topic_decref(topic);
		}
	}
}

struct topic_s *
topics_db_get_topic(struct topics_db *db, const char *topic, const time_t now) {
	const tommy_hash_t topic_hash =
			tommy_hash_u64(hash_init, topic, strlen(topic));

	pthread_mutex_lock(&db->lock);
	/*
	 *  Retrieve topic
	 */
	struct topic_s *ret = tommy_hashdyn_search(
			&db->hashtable, void_topics_cmp, topic, topic_hash);
	if (ret) {
		// Prepare for update in lru
		topic_list_remove(&db->lru, ret);
	} else {
		// Create a new one, and introduce it in db
		pthread_mutex_unlock(&db->lock);
		ret = new_topic_s(topic);
		if (NULL == ret) {
			/// Error in allocation, better return now
			return NULL;
		}
		pthread_mutex_lock(&db->lock);

		/* Do we still have to insert it? */
		struct topic_s *aux = tommy_hashdyn_search(&db->hashtable,
							   void_topics_cmp,
							   topic,
							   topic_hash);
		if (aux) {
			// Other thread has inserted topic!
			topic_decref(ret);
			ret = aux;

			// Update topic
			topic_list_remove(&db->lru, ret);
			goto rkt_found;
		} else {
			tommy_hashdyn_insert(&db->hashtable,
					     &ret->hashtable_node,
					     ret,
					     topic_hash);
		}
	}

rkt_found:
	purge_topics_older_than(db, now - topic_live_time_s);
	ret->timestamp = now;
	topic_list_insert_tail(&db->lru, ret);
	ATOMIC_OP(add, fetch, &ret->refcnt, 1);

	pthread_mutex_unlock(&db->lock);

	return ret;
}

void topics_db_done(struct topics_db *db) {
	tommy_hashdyn_foreach(&db->hashtable, void_topic_decref);
	tommy_hashdyn_done(&db->hashtable);
	pthread_mutex_destroy(&db->lock);
	free(db);
}
