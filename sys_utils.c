#include "config.h"
#include <stdio.h>

#ifdef HAVE_SYS_LOADAVG_H
#include <sys/loadavg.h>
#endif

double get_load_avg(void)
{
#ifdef HAVE_LOADAVG
    double loadavg[1];
    int n = getloadavg(loadavg, 1);

    if (n < 1)
        return 1000;
    else
        return loadavg[0];
#else
    FILE *loadavg = fopen("/proc/loadavg", "r");
    int avg = 1000;

    if (!loadavg) {
        fprintf(stderr, "failed to read /proc/loadavg");
        return 1000;
    }
    if (fscanf(loadavg, "%d", &avg) != 1) {
        fprintf(stderr, "failed to parse /proc/loadavg");
        fclose(loadavg);
        return 1000;
    }
    fclose(loadavg);

    return avg;
#endif
}
