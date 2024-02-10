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

#ifndef G_LOGGER_H
#define G_LOGGER_H

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int foreground;

void g_logger(int log_level, const char *format, ...);

const char *g_logger_level_name(int log_level);

#ifdef __cplusplus
}
#endif

#endif
