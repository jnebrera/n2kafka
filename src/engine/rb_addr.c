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

#include "rb_addr.h"

#include "util/util.h"

#include <librd/rdlog.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <syslog.h>

const char *
sockaddr2str(char *buf, size_t buf_size, struct sockaddr *sockaddr) {
	const void *addr_buf = NULL;
	const char *ret = NULL;

	switch (sockaddr->sa_family) {
	case AF_INET:
		addr_buf = &((struct sockaddr_in *)sockaddr)->sin_addr;
		break;
	case AF_INET6:
		addr_buf = &((struct sockaddr_in6 *)sockaddr)->sin6_addr;
		break;
	default:
		break;
	}

	if (NULL == addr_buf) {
		errno = EAFNOSUPPORT;
	} else {
		ret = inet_ntop(sockaddr->sa_family, addr_buf, buf, buf_size);
	}

	if (NULL == ret) {
		rdlog(LOG_ERR,
		      "Can't print client address: %s",
		      gnu_strerror_r(errno));
	}

	return ret;
}
