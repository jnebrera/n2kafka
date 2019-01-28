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

#include "config.h"

#if WITH_EXPAT

#include "src/decoder/decoder_api.h"

#include "util/string.h"

#include <expat.h>
#include <yajl/yajl_gen.h>

#include <limits.h>
#include <stddef.h>
#include <sys/types.h>

struct zz_session;

/// XML session
typedef struct zz_xml_session {
	/// JSONoutput buffer
	struct {
		/// Session string to send to kafka
		string yajl_gen_buf;

		/// Last JSON object open brace offset
		ssize_t last_open_map;

		/// Generator stack position
		size_t stack_pos;

		/// Boolean vector/stack to check if we are printing text
		char printing_text[128 / CHAR_BIT];
	} json_buf;

	/// Expat handler
	XML_Parser expat_handler;

	/// JSON generator
	yajl_gen yajl_gen;

	/// Return code.
	enum decoder_callback_err rc;
} zz_xml_session;

/**
 * @brief      Allocates yajl JSON handler in zz session
 *
 * @param      sess  The session
 *
 * @return     0 in case of right allocation, 1 in other case.
 */
int new_zz_session_xml(struct zz_session *sess);

#endif // WITH_EXPAT
