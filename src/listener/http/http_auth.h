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

#include <stdbool.h>
#include <stdio.h>

/**
 * @brief      Extract HTTP authenticate data
 *
 * @param      dst       The destination to save the data. Can be NULL to check
 *                       for the needed length. The caller is responsible for
 *                       allocate and destroy the space. <0 if can't fseek the
 *                       file. The caller must check if all database fit in the
 *                       supplied dst buffer.
 * @param[in]  dst_size  The destination size
 * @param      src       The source file of the data. The format must be the
 *                       same as nginx htpasswd.
 *
 * @return     Destination size to save all the database.
 */
ssize_t http_auth_extract_data(void *dst, size_t dst_size, FILE *src);

/**
 * @brief      Validate an user against already computed authenticate database
 *
 * @param      connection  The connection to authenticate
 * @param[in]  db          The database to authenticate against.
 *
 * @return     True if the user is valid, false otherwise.
 */
bool http_authenticate(const char *user, const char *password, const void *db);
