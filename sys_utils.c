/*
Copyright © 2013 mod_tile contributors

This file is part of mod_tile.

mod_tile is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation, either version 2 of the License, or (at your
option) any later version.

mod_tile is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with mod_tile.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "config.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_SYS_LOADAVG_H
#include <sys/loadavg.h>
#endif

double get_load_avg(void)
{
#ifdef HAVE_GETLOADAVG
    double loadavg[1];
    int n = getloadavg(loadavg, 1);

    if (n < 1)
        return 1000.0;
    else
        return loadavg[0];
#else
    FILE *loadavg = fopen("/proc/loadavg", "r");
    double avg = 1000.0;

    if (!loadavg) {
        fprintf(stderr, "failed to read /proc/loadavg");
        return 1000.0;
    }
    if (fscanf(loadavg, "%lf", &avg) != 1) {
        fprintf(stderr, "failed to parse /proc/loadavg");
        fclose(loadavg);
        return 1000.0;
    }
    fclose(loadavg);

    return avg;
#endif
}
