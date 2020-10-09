/*
 * Copyright (c) 2007 - 2020 by mod_tile contributors (see AUTHORS file)
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

#ifndef CACHEEXPIRE_H
#define CACHEEXPIRE_H

#ifdef __cplusplus
extern "C" {
#endif

#define HTCP_EXPIRE_CACHE 1
#define HTCP_EXPIRE_CACHE_PORT "4827"


void cache_expire(int sock, char * host, char * uri, int x, int y, int z);
int init_cache_expire(char * htcphost);

#ifdef __cplusplus
}
#endif

#endif
