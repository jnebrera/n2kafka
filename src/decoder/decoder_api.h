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
	void (*callback)(char *buffer,
			 size_t buf_size,
			 const keyval_list_t *props,
			 void *listener_callback_opaque,
			 void **sessionp);

	int (*init)();			     ///< Init decoder global config
	int (*reload)(const json_t *config); ///< Reload decoder.
	void (*done)();			     ///< Finish decoder global config

	/// Per-listener decoder information creator
	int (*opaque_creator)(const json_t *config, void **opaque);
	/// Per listener decoder information reload
	int (*opaque_reload)(const json_t *config, void *opaque);
	/// Per listener decoder information destructor
	void (*opaque_destructor)(void *opaque);

	int (*flags)(); ///< Decoders flags
} n2k_decoder;
