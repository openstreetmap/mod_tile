/*
 * Copyright (c) 2007 - 2023 by mod_tile contributors (see AUTHORS file)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see http://www.gnu.org/licenses/.
 */

#ifndef RENDERD_CONFIG_H
#define RENDERD_CONFIG_H

#include "render_config.h"
#include "renderd.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int num_slave_threads;
extern renderd_config config;
extern renderd_config config_slaves[MAX_SLAVES];
extern xmlconfigitem maps[XMLCONFIGS_MAX];

int min_max_int_opt(const char *opt_arg, const char *opt_type_name, int minimum, int maximum);
void process_config_file(const char *config_file_name, int active_slave, int log_level);

#ifdef __cplusplus
}
#endif
#endif
