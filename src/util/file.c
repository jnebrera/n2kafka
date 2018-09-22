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

#include "file.h"

#include "util.h"

off64_t file_size(FILE *fp) {
	const off64_t curr_off = ftello(fp);
	if (unlikely(curr_off == (off64_t)-1)) {
		return (off64_t)-1;
	}

	const int seek_end_rc = fseeko(fp, 0, SEEK_END);
	if (unlikely(seek_end_rc != 0)) {
		return (off64_t)-1;
	}

	const off64_t bytes = ftello(fp);

	const int seek_restore_rc = fseeko(fp, curr_off, SEEK_SET);
	if (unlikely(seek_restore_rc != 0)) {
		return (off64_t)-1;
	}

	return bytes;
}
