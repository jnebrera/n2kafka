/*
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
** Copyright (C) 2018, Wizzie S.L.
** Author: Eugenio Perez <eupm90@gmail.com>
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

#include "util.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>

#ifndef static_assert
#define static_assert(...)
#endif

#define STRING_MIN_SIZE 4096

#define smax(a, b)                                                             \
	({                                                                     \
		typeof(a) __a = a, __b = b;                                    \
		(__a > __b) ? __a : __b;                                       \
	})

/// String with automatic grow
typedef struct string {
	char *buf;   ///< Actual buffer
	size_t size; ///< Size occupied in buffer. Allocated size will always
		     ///< be (log2(size)<<1), with a minimum of STRING_MIN_SIZE.
} string;

#define N2K_STRING_INITIALIZER                                                 \
	(string) {                                                             \
	}

static int __attribute__((unused)) string_init(struct string *str) {
	*str = N2K_STRING_INITIALIZER;
	return 0;
}

static void string_done(struct string *str) __attribute__((unused));
static void string_done(struct string *str) {
	free(str->buf);
	memset(str, 0, sizeof(*str));
}

static size_t string_size(const string *str) __attribute__((unused));
static size_t string_size(const string *str) {
	return str->size;
}

static size_t __attribute__((unused)) next_pow2(size_t x) {
	static_assert(sizeof(x) == sizeof(unsigned long),
		      "Please expand this function!");
	// bits of size_t
	static const size_t bits_x = CHAR_BIT * sizeof(x);
	return (x && x - 1) ? 1u << (bits_x - (size_t)__builtin_clzl(x - 1))
			    : 1;
}

static size_t __attribute__((unused)) string_allocated(const string *str) {
	return str->size == 0 ? 0
			      : str->size < STRING_MIN_SIZE
						? STRING_MIN_SIZE
						: next_pow2(str->size);
}

static size_t __attribute__((unused)) string_grow(string *str, size_t delta) {
	const size_t new_size = str->size + delta;
	const size_t new_allocated_size = next_pow2(new_size);
	if (!(string_allocated(str) < new_allocated_size)) {
		// No need to grow
		return string_allocated(str);
	}

	const size_t new_allocated = smax(new_allocated_size, STRING_MIN_SIZE);
	char *new_buf = realloc(str->buf, new_allocated);
	if (NULL == new_buf) {
		// Failure reallocating
		return string_allocated(str);
	}

	str->buf = new_buf;
	return new_allocated;
}

static int __attribute__((unused))
string_append(string *str, const char *data, size_t data_size) {
	const size_t new_size = string_grow(str, data_size);
	if (unlikely(new_size < str->size + data_size)) {
		// Can't append
		return -1;
	}

	memcpy(&str->buf[str->size], data, data_size);
	str->size += data_size;
	return 0;
}
