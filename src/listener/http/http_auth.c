/*
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
** Copyright (C) 2018-2019, Wizzie S.L.
** Author: Eugenio Perez <eupm90@gmail.com>
**
** This file is part of n2kafka.
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

#include "http_auth.h"

#include "util/util.h"

#include <librd/rd.h>

#include <assert.h>
#include <string.h>

typedef bool (*hashing_algorithm_callback)(const char *provided_password,
					   const char *htfile_password);

static const char *plain_hashing_callback_prefix = ":{PLAIN}";

/**
 * @brief      Callback to decode ${PLAIN} passwords
 *
 * @param[in]  provided_password  The provided password
 * @param[in]  htfile_password    The htfile stored password
 *
 * @return     True if password is OK, false otherwise
 */
static bool plain_hashing_callback(const char *provided_password,
				   const char *htfile_password) {
	assert(0 == strncmp(provided_password,
			    plain_hashing_callback_prefix,
			    strlen(plain_hashing_callback_prefix)));

	return 0 ==
	       strcmp(htfile_password + strlen(plain_hashing_callback_prefix),
		      provided_password);
}

/**
 * @brief      Tokenize the user:pass line.
 *
 * @note       This function assumes that the line is complete, i.e., it has no
 *             way to detect that the full line has been read from the file. If
 *             the file does NOT end with a newline, the endptr will be 0, and
 *             the caller needs to know if full file has been read or if the
 *             provided buffer was not enough, and there is still line to read!
 *
 * @param[in]  line    The line
 * @param      colon   The colon that separate user and password
 * @param      endptr  The line endptr
 */
static void
tokenize_user_pass_line(const char *line, char **colon, char **endptr) {
	assert(line);
	assert(colon);
	assert(endptr);

	*endptr = strchrnul(line, ':');
	if (unlikely(**endptr != ':')) {
		return;
	}

	*colon = *endptr;
	*endptr = strchrnul(*colon, '\n');
}

/**
 * @brief      Find the hashing callback to provided password
 *
 * @param[in]  colon  The username:password separation colon
 *
 * @return     The callback to use with this password
 */
static hashing_algorithm_callback password_hashing_callback(const char *colon) {
	struct {
		const char *prefix;
		hashing_algorithm_callback callback;
	} callbacks[] = {{
			plain_hashing_callback_prefix,
			plain_hashing_callback,
	}};

	for (size_t i = 0; i < RD_ARRAYSIZE(callbacks); ++i) {
		if (0 == strncmp(colon,
				 callbacks[i].prefix,
				 strlen(callbacks[i].prefix))) {
			return callbacks[i].callback;
		}
	}

	return NULL;
}

/**
 * @brief      Extract the authorization data from htpassword file
 *
 * @param      dst       The data destination buffer
 * @param[in]  dst_size  The provided data destination buffer size
 * @param      src       The file source
 *
 * @return     Number of bytes written or that should have been written if
 * dst_size < 0, <0 on error
 */
static ssize_t http_auth_extract_data0(char *dst, size_t dst_size, FILE *src) {
	size_t line = 1;
	ssize_t rc = 0;

	char *write_cursor = dst;
	struct {
		char *buf;
		size_t size;
	} getline_buf = {
			.buf = NULL,
			.size = 0,
	};

	while (true) {
		const ssize_t getline_rc = getline(
				&getline_buf.buf, &getline_buf.size, src);

		// Chop all newlines
		if (getline_rc < 0) {
			const bool end_of_file_reached = feof(src);
			if (unlikely(!end_of_file_reached)) {
				const char *err_str = gnu_strerror_r(errno);
				rdlog(LOG_ERR,
				      "Error reading file: %s",
				      err_str);
				rc = -1;
			}

			break;
		}

		//
		// Line treatment
		//
		char *colon = NULL, *endptr = NULL;
		tokenize_user_pass_line(getline_buf.buf, &colon, &endptr);

		if (unlikely(NULL == colon)) {
			// Full line read and couldn't locate colon.
			rdlog(LOG_ERR,
			      "Line %zu of http passwords file ill formed: No "
			      "colon found",
			      line);

			goto end_of_line;
		}

		// Proper line. Supported password?
		if (unlikely(NULL == password_hashing_callback(colon))) {
			rdlog(LOG_ERR,
			      "Line %zu of http passwords file ill "
			      "formed: Unsupported hashing type",
			      line);
			goto end_of_line;
		}

		// All OK!
		const size_t written = (size_t)(write_cursor - dst);
		const size_t entry_len = (size_t)(endptr - getline_buf.buf) + 1;
		rc += (ssize_t)entry_len;

		if (written + entry_len < dst_size) {
			memcpy(write_cursor, getline_buf.buf, entry_len - 1);
			write_cursor[entry_len] = '\0';
			write_cursor += entry_len;
		}

	end_of_line:
		line++;
	}

	free(getline_buf.buf);
	return rc;
}

/**
 * @brief      See http_auth_extract_data0
 */
ssize_t http_auth_extract_data(void *dst, size_t dst_size, FILE *src) {
	const off64_t curr_off = ftello(src);

	if (unlikely(curr_off) < 0) {
		return -1;
	}

	const ssize_t rc = http_auth_extract_data0(dst, dst_size, src);

	const int fseeko_rc = fseeko(src, curr_off, SEEK_SET);
	if (unlikely(fseeko_rc != 0)) {
		return -1;
	}

	return rc;
}

/**
 * @brief      Authenticate an user against a valid database
 *
 * @param[in]  user      The user
 * @param[in]  password  The password
 * @param[in]  db        The database
 *
 * @return     True if the user is allowed, false otherwise
 */
bool http_authenticate(const char *user, const char *password, const void *db) {
	static const char user_pass_separator = ':';
	const char *cursor = db;
	const size_t user_strlen = strlen(user);

	while (*cursor != '\n') {
		const char *end_of_line = strchrnul(cursor, '\n');

		// user-password line between cursor and newline
		const char *colon_sepparator =
				strchr(cursor, user_pass_separator);

		// http_auth_extract_data should only allow valid lines
		assert(colon_sepparator);
		assert(colon_sepparator >= cursor);

		if ((size_t)(colon_sepparator - cursor) != user_strlen) {
			cursor = end_of_line;
			continue;
		}

		if (0 == strncmp(user, cursor, user_strlen)) {
			// Right user, is the right password?
			const hashing_algorithm_callback cb =
					password_hashing_callback(
							colon_sepparator);
			return cb(password, colon_sepparator);
		}
	}

	return false;
}
