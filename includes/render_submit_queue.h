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

#ifndef RENDER_SUBMIT_QUEUE_H
#define RENDER_SUBMIT_QUEUE_H

#include <sys/time.h>

#include "render_config.h"

#ifdef __cplusplus
extern "C" {
#endif

	/**
	 * @brief Singly linked queue node representing a metatile job to submit to `renderd`.
	 */
	struct qItem {
		/** @brief Heap-allocated target map configuration (`xmlname`) string. */
		char *mapname;
		/** @brief Tile X, Y, and zoom coordinates of the metatile to render. */
		int x, y, z;
		/** @brief Pointer to the next queued submission item (`NULL` at tail). */
		struct qItem *next;
	};

	/**
	 * @brief Aggregated rendering latency and throughput counters for a single zoom level.
	 */
	struct speed_stat {
		/** @brief Minimum observed metatile render duration in seconds. */
		time_t time_min;
		/** @brief Maximum observed metatile render duration in seconds. */
		time_t time_max;
		/** @brief Cumulative metatile render duration in seconds across all rendered items. */
		time_t time_total;
		/** @brief Total number of metatiles rendered at this zoom level. */
		int noRendered;
	};

	/**
	 * @brief Per-zoom-level collection of rendering speed statistics (`0` through `MAX_ZOOM`).
	 */
	struct speed_stats {
		/** @brief Array of per-zoom latency and count statistics indexed by zoom level `z`. */
		struct speed_stat stat[MAX_ZOOM + 1];
	};

	int make_connection(const char *spath);
	void check_load(void);
	void display_rate(struct timeval start, struct timeval end, int num);
	void enqueue(const char *xmlname, int x, int y, int z);
	void finish_workers(void);
	void print_statistics(void);
	void spawn_workers(int num, const char *socketpath, int maxLoad);
	void *thread_main(void *arg);
	void wait_for_empty_queue(void);

#ifdef __cplusplus
}
#endif

#endif /* RENDER_SUBMIT_QUEUE_H */
