/*
 * Copyright (c) 2007 - 2026 by mod_tile contributors (see AUTHORS file)
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

#ifndef CACHE_EXPIRE_H
#define CACHE_EXPIRE_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Feature flag enabling HTCP (Hyper Text Caching Protocol) tile cache purging. */
#define HTCP_EXPIRE_CACHE 1
/** @brief Default UDP destination port string for HTCP cache purge (`CLR`) requests. */
#define HTCP_EXPIRE_CACHE_PORT "4827"

	void cache_expire(int sock, const char *host, const char *uri, int x, int y, int z);
	int init_cache_expire(const char *htcphost);

#ifdef __cplusplus
}
#endif

#endif /* CACHE_EXPIRE_H */
