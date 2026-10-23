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

#ifndef RENDERSUBQUEUE_H
#define RENDERSUBQUEUE_H

#include <sys/time.h>

#include "render_config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct qItem {
	char *mapname;
	int x, y, z;
	struct qItem *next;
};

struct speed_stat {
	time_t time_min;
	time_t time_max;
	time_t time_total;
	int noRendered;
};

struct speed_stats {
	struct speed_stat stat[MAX_ZOOM + 1];
};

int make_connection(const char *spath);
void check_load(void);
void enqueue(const char *xmlname, int x, int y, int z);
void finish_workers(void);
void print_statistics(void);
void spawn_workers(int num, const char *socketpath, int maxLoad);
void *thread_main(void *arg);
void wait_for_empty_queue(void);

#ifdef __cplusplus
}

#endif

#endif
