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

#ifndef GEN_TILE_H
#define GEN_TILE_H

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

	/**
	 * @brief Priority queues and lifecycle states tracked by the render request scheduler.
	 */
	enum queueEnum {
		/** @brief Standard interactive tile render queue (`cmdRender`). */
		queueRequest,
		/** @brief High-priority interactive tile render queue (`cmdRenderPrio`). */
		queueRequestPrio,
		/** @brief Bulk/batch tile render queue (`cmdRenderBulk`). */
		queueRequestBulk,
		/** @brief Background dirty-tile re-render queue (`cmdDirty`). */
		queueDirty,
		/** @brief Active rendering state (dequeued and currently being rendered by a worker thread). */
		queueRender,
		/** @brief Coalesced duplicate request linked onto an existing in-flight or queued `struct item`. */
		queueDuplicate,
		/** @brief Low-priority interactive tile render queue (`cmdRenderLow`). */
		queueRequestLow
	};

	/**
	 * @brief Node representing a queued or active metatile render request in the scheduler.
	 */
	struct item {
		/** @brief Next item in the doubly linked priority queue list. */
		struct item *next;
		/** @brief Previous item in the doubly linked priority queue list. */
		struct item *prev;
		/** @brief Wire protocol request payload (`cmd`, `x`, `y`, `z`, `xmlname`, `options`, etc.). */
		struct protocol req;
		/** @brief Metatile-aligned base X and Y coordinates (`x & ~(METATILE - 1)`, `y & ~(METATILE - 1)`). */
		int mx, my;
		/** @brief Client socket file descriptor awaiting completion notification (`FD_INVALID` if none). */
		int fd;
		/** @brief Singly linked list of duplicate requests coalesced onto this metatile job. */
		struct item *duplicates;
		/** @brief Current queue or state (`queueRender`, `queueDuplicate`, etc.) containing this item. */
		enum queueEnum inQueue;
		/** @brief Original priority queue into which this request was initially submitted. */
		enum queueEnum originatedQueue;
	};

	// int render(Map &m, int x, int y, int z, const char *filename);
	void *render_thread(void *);
	struct item *fetch_request(void);
	void delete_request(struct item *item);
	void render_init(const char *plugins_dir, const char *font_dir, int font_recurse);

#ifdef __cplusplus
}
#endif

#endif /* GEN_TILE_H */
