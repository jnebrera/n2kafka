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

#include "listener_api.h"

#include "decoder/decoder_api.h"

#include <librd/rdlog.h>

#include <inttypes.h>

int listener_init(struct listener *this,
		  uint16_t port,
		  const struct n2k_decoder *decoder,
		  const json_t *decoder_conf) {
	this->decoder = decoder;
	this->reload = listener_reload;
	this->join = listener_join;
	this->port = port;

	if (decoder->opaque_creator) {
		const int opaque_creator_rc = decoder->opaque_creator(
				decoder_conf, &this->decoder_opaque);
		if (opaque_creator_rc != 0) {
			rdlog(LOG_ERR,
			      "Can't create opaque for listener on port "
			      "%" PRIu16,
			      port);
			return opaque_creator_rc;
		}
	}

	return 0;
}

int listener_reload(struct listener *this, struct json_t *new_config) {
	const struct n2k_decoder *decoder = this->decoder;
	if (decoder->opaque_reload) {
		rdlog(LOG_INFO, "Reloading opaque");
		return decoder->opaque_reload(new_config, this->decoder_opaque);
	} else {
		rdlog(LOG_INFO, "Not reload opaque provided");
		return 0;
	}
}

void listener_join(struct listener *this) {
	const struct n2k_decoder *decoder = this->decoder;
	if (decoder->opaque_destructor) {
		decoder->opaque_destructor(this->decoder_opaque);
	}
}
