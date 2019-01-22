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

#include "util/string.h"

#include <stddef.h>

#include <yajl/yajl_parse.h>

struct zz_session;

typedef struct zz_json_session {
	yajl_handle yajl_handler;

	/// Parsing stack position
	size_t stack_pos;
	/// Per chunk information
	struct {
		const char *in_buffer;     ///< current yajl_parse call chunk
		const char *last_open_map; ///< Last seen open map
	} http_chunk;

	/// Previous chunk object, in case that JSON object is cut in the middle
	struct {
		string last_object;
	} http_prev_chunk;
} zz_json_session;

/**
 * @brief      Allocates yajl JSON handler in zz session
 *
 * @param      sess  The session
 *
 * @return     0 in case of right allocation, 1 in other case.
 */
int new_zz_session_json(struct zz_session *sess);
