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
