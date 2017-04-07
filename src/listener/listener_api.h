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

#include "decoder/decoder_api.h"

#include <util/pair.h>

#include <jansson.h>

#include <stdint.h>

/// Listener
typedef struct listener {
	// You can override this functions if you promise to call them in
	// override version.
	void (*join)(struct listener *listener); ///< Join listener
	int (*reload)(struct listener *listener,
		      struct json_t *new_config); ///< Reload listener

	// Private data - Do not use directly
	const struct n2k_decoder *decoder; ///< Decoder to use
	void *decoder_opaque;		   ///< Decode per-listener opaque
	uint16_t port;			   ///< as listener ID
	LIST_ENTRY(listener) entry;	///< Listener list entry
} listener;

/// @todo return 0 to say OK!
static void __attribute__((unused)) listener_decode(const struct listener *this,
						    char *buffer,
						    size_t buf_size,
						    const keyval_list_t *props,
						    void *session) {
	this->decoder->callback(
			buffer, buf_size, props, this->decoder_opaque, session);
}

int listener_reload(struct listener *listener, struct json_t *new_config);

/// @note This join DOES NOT free listener used space
void listener_join(struct listener *listener);

int listener_init(struct listener *l,
		  uint16_t port,
		  const struct n2k_decoder *decoder,
		  const json_t *decoder_conf);

/** Listener factory */
typedef struct n2k_listener_factory {
	const char *(*name)(); ///< Registered listener name.

	struct listener *(*create)(const json_t *config,
				   const struct n2k_decoder *decoder);
} n2k_listener_factory;
