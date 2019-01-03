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

#pragma once

#include <util/pair.h>

#include <jansson.h>

/// Possible decoders errors return code.
enum decoder_callback_err {
	DECODER_CALLBACK_OK = 0,
	// Kafka errors - Server side
	DECODER_CALLBACK_BUFFER_FULL,

	// Client side errors
	DECODER_CALLBACK_INVALID_REQUEST,

	// Kafka errors - Client side
	DECODER_CALLBACK_UNKNOWN_TOPIC,
	DECODER_CALLBACK_UNKNOWN_PARTITION,
	DECODER_CALLBACK_MSG_TOO_LARGE,

	// HTTP errors
	DECODER_CALLBACK_HTTP_METHOD_NOT_ALLOWED,
	DECODER_CALLBACK_RESOURCE_NOT_FOUND /* Just return 404 */,
	DECODER_CALLBACK_MEMORY_ERROR,
	DECODER_CALLBACK_GENERIC_ERROR,
};

/** Decoder API
  All functions are thread-safe except init & done, and call callback() with
  the same opaque from two different threads
  */
typedef struct n2k_decoder {
	const char *(*name)();		   ///< Registered decoder name.
	const char *(*config_parameter)(); ///< Name of config parameter

	/** Callback that the listener needs to call for each data received.
	    @param buffer JSON chunk buffer
	    @param buf_size JSON chunk buffer size
	    @param props HTTP properties
	    @param t_decoder_opaque Decoder opaque - unused
	    @param t_session Decoder session
	    @return Proper decoder error
	    */
	enum decoder_callback_err (*callback)(const char *buffer,
					      size_t buf_size,
					      const keyval_list_t *props,
					      void *listener_callback_opaque,
					      const char **response,
					      size_t *response_size,
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
