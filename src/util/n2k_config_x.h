//
// Copyright (C) 2014-2016, Eneo Tecnologia S.L.
// Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
// Copyright (C) 2018-2019, Wizzie S.L.
// Author: Eugenio Perez <eupm90@gmail.com>
//
// This file is part of n2kafka.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#pragma once

// The next function or macros operates in a X-MACRO list of the way:
// X(struct_type, json_unpack_type, json_name, struct_name, env_name,
// str_to_value_function, default)

/// Destination configuration structure members declaration
#define X_STRUCT_N2K_CONFIG(                                                   \
		struct_type, json_unpack_type, json_name, struct_name, ...)    \
	struct_type struct_name;

/// Initialize destination configuration struct with the defaults.
#define X_DEFAULTS_N2K_CONFIG(struct_type,                                     \
			      json_unpack_type,                                \
			      json_name,                                       \
			      struct_name,                                     \
			      env_name,                                        \
			      str_to_value_function,                           \
			      t_default)                                       \
	.struct_name = t_default,

/// Get the environment variables and set them to configuration struct
#define X_ENVIRON_N2K_CONFIG(struct_type,                                      \
			     json_unpack_type,                                 \
			     json_name,                                        \
			     struct_name,                                      \
			     env_name,                                         \
			     str_to_value_function,                            \
			     ...)                                              \
	if (NULL != env_name) {                                                \
		const char *env_val = getenv(env_name);                        \
		if (env_val) {                                                 \
			handler_args.struct_name =                             \
					str_to_value_function(env_val);        \
		}                                                              \
	}

/// jsnsson unpack format of the configuration variable
#define X_JANSSON_UNPACK_FORMAT(struct_type, json_unpack_type, ...)            \
	"s" json_unpack_type

/// jansson unpack argument of the configuration variable
#define X_JANSSON_UNPACK_ARGS(                                                 \
		struct_type, json_unpack_type, json_name, struct_name, ...)    \
	/* */ #json_name, &handler_args.struct_name,
