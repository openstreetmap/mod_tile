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

#ifndef RENDERD_H
#define RENDERD_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAVE_DAEMON
int daemon(int nochdir, int noclose);
#endif

#include "gen_tile.h"
#include "protocol.h"
#include <limits.h>

#define INILINE_MAX 256
#define MAX_SLAVES 5

typedef struct {
	const char *iphostname;
	const char *mapnik_font_dir;
	const char *mapnik_plugins_dir;
	const char *pid_filename;
	const char *socketname;
	const char *stats_filename;
	const char *tile_dir;
	int ipport;
	int mapnik_font_dir_recurse;
	int num_threads;
} renderd_config;

typedef struct {
	char xmlname[XMLCONFIG_MAX];
	char xmlfile[PATH_MAX];
	char xmluri[PATH_MAX];
	char host[PATH_MAX];
	char htcpip[PATH_MAX];
	char tile_dir[PATH_MAX];
	char output_format[INILINE_MAX];
	char parameterization[PATH_MAX];
	int tile_px_size;
	double scale_factor;
	int min_zoom;
	int max_zoom;
	int num_threads;
} xmlconfigitem;

extern struct request_queue *render_request_queue;

void statsRenderFinish(int z, long time);
void request_exit(void);
void send_response(struct item *item, enum protoCmd rsp, int render_time);
enum protoCmd rx_request(struct protocol *req, int fd);

#ifdef __cplusplus
}
#endif
#endif
