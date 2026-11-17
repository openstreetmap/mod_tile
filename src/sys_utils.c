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

/**
 * @brief Retrieve the system's 1-minute load average.
 *
 * Queries the 1-minute load average via `getloadavg()` when `HAVE_GETLOADAVG` is
 * defined, falling back to reading and parsing the first floating-point field
 * from `/proc/loadavg` on Linux-like systems. If the load average cannot be
 * obtained or parsed, returns `1000.0` as a high sentinel value so callers treat
 * the host as overloaded.
 *
 * @return Current 1-minute load average, or `1000.0` on failure.
 */
double get_load_avg(void)
{
#ifdef HAVE_GETLOADAVG
	/* Single-element array receiving the 1-minute load average from getloadavg() */
	double loadavg[1];
	/* Number of load average samples retrieved (1 on success, -1 on error) */
	int n = getloadavg(loadavg, 1);

	if (n < 1) {
		return 1000.0;
	} else {
		return loadavg[0];
	}

#else  /* HAVE_GETLOADAVG */
	/* File stream handle for reading /proc/loadavg when getloadavg() is unavailable */
	FILE *loadavg = fopen("/proc/loadavg", "r");
	/* Parsed 1-minute load average, defaulting to high sentinel 1000.0 on error */
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
#endif /* HAVE_GETLOADAVG */
}
