/*
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
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

#include <util/pair.h>

#include <jansson.h>

/** Decoder API
  All functions are thread-safe except init & done, and call callback() with
  the same opaque from two different threads
  */
typedef struct n2k_decoder {
	const char *(*name)();		   ///< Registered decoder name.
	const char *(*config_parameter)(); ///< Name of config parameter

	/// Callback that the listener needs to call for each data received
	void (*callback)(const char *buffer,
			 size_t buf_size,
			 const keyval_list_t *props,
			 void *listener_callback_opaque,
			 void *sessionp);

	int (*init)();			     ///< Init decoder global config
	int (*reload)(const json_t *config); ///< Reload decoder.
	void (*done)();			     ///< Finish decoder global config

	/// LISTENER-LOCAL CONFIGURATION
	/// Per-listener decoder information creator
	int (*opaque_creator)(const json_t *config, void **opaque);
	/// Per listener decoder information reload
	int (*opaque_reload)(const json_t *config, void *opaque);
	/// Per listener decoder information destructor
	void (*opaque_destructor)(void *opaque);

	/// SESSION-LOCAL INFORMATION
	/// Per-session information. You have to use callback with this!
	int (*new_session)(void *sessionp,
			   void *listener_opaque,
			   const keyval_list_t *props);

	/// Finish this session
	void (*delete_session)(void *sessionp);

	/// Session size, in order to pre-allocate it
	size_t (*session_size)();
} n2k_decoder;
