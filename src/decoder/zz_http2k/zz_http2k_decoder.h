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

#include "zz_database.h"

#include "util/pair.h"

#include <jansson.h>
#include <librd/rdavl.h>
#include <librd/rdsysqueue.h>
#include <librdkafka/rdkafka.h>

#include <pthread.h>
#include <stdint.h>
#include <string.h>

/* All functions are thread-safe here, excepting free_valid_mse_database */
struct json_t;
struct zz_config {
#ifndef NDEBUG
/// MAGIC to check zz_config between void * conversions
#define ZZ_CONFIG_MAGIC 0xbc01a1cbc01a1cL
	/// This value always have to be ZZ_CONFIG_MAGIC
	uint64_t magic;
#endif
	struct zz_database database;
};

#ifdef ZZ_CONFIG_MAGIC
/// Checks that zz_config magic field has the right value
#define assert_zz_config(cfg)                                                  \
	do {                                                                   \
		assert(ZZ_CONFIG_MAGIC == (cfg)->magic);                       \
	} while (0)
#else
#define assert_zz_config(cfg)
#endif

int parse_zz_config(void *_db, const struct json_t *zz_config);
/** Release all resources used */
void zz_decoder_done(void *zz_config);
/** Does nothing, since this decoder does not save anything related to
    listener
    */
int zz_decoder_reload(void *_db, const struct json_t *zz_config);

int zz_opaque_creator(struct json_t *config, void **opaque);
int zz_opaque_reload(struct json_t *config, void *opaque);
void zz_opaque_done(void *opaque);
void zz_decode(char *buffer,
	       size_t buf_size,
	       const keyval_list_t *props,
	       void *listener_callback_opaque,
	       void **decoder_sessionp);
