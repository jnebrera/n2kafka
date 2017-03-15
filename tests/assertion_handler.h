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

#include "stdio.h"
#include "stdlib.h"

#include <pthread.h>
#include <string.h>
#include <sys/queue.h>

// Public structs
struct assertion_e {
	TAILQ_ENTRY(assertion_e) tailq;
	char *str;
};

struct value_e {
	TAILQ_ENTRY(value_e) tailq;
	char *str;
	size_t len;
};

// Private structs
struct assertion_handler_s;

// Functions
struct assertion_handler_s *assertion_handler_new();
void assertion_handler_push_assertion(
		struct assertion_handler_s *assertion_handler,
		struct assertion_e *assertion);
void assertion_handler_push_value(struct assertion_handler_s *assertion_handler,
				  struct value_e *value);
int assertion_handler_assert(struct assertion_handler_s *assertion_handler);
void assertion_handler_destroy(struct assertion_handler_s *assertion_handler);
