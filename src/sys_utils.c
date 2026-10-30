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

#include <stdlib.h>

#include "config.h"

#ifdef HAVE_SYS_LOADAVG_H
#include <sys/loadavg.h>
#endif

double get_load_avg(void)
{
#ifdef HAVE_GETLOADAVG
	double loadavg[1];
	int n = getloadavg(loadavg, 1);

	if (n < 1) {
		return 1000.0;
	} else {
		return loadavg[0];
	}

#else
	FILE *loadavg = fopen("/proc/loadavg", "r");
	double avg = 1000.0;

	if (!loadavg) {
		g_logger(G_LOG_LEVEL_ERROR, "failed to read /proc/loadavg");
		return 1000.0;
	}

	if (fscanf(loadavg, "%lf", &avg) != 1) {
		g_logger(G_LOG_LEVEL_ERROR, "failed to parse /proc/loadavg");
		fclose(loadavg);
		return 1000.0;
	}

	fclose(loadavg);

	return avg;
#endif
}
