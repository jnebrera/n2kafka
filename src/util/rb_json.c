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

#include "rb_json.h"

#include "util.h"

#include <string.h>

int json_object_update_missing_copy(json_t *dst, const json_t *src) {
	const char *key = NULL;
	const json_t *value = NULL;
	/* We promise that we will not modify this */

	if (!json_is_object(src) || !json_is_object(dst))
		return -1;

	// We only do read-only actions, so we can do this
	json_t *my_src = const_cast(src);
	json_object_foreach(my_src, key, value) {
		if (NULL == json_object_get(dst, key)) {
			json_t *new_json = json_deep_copy(value);
			json_object_set_new_nocheck(dst, key, new_json);
		}
	}

	return 0;
}
