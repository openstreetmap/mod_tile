#ifndef DAEMON_H
#define DEEMON_H

#include <limits.h> /* for PATH_MAX */

#include "protocol.h"

#define INILINE_MAX 256

typedef struct {
    char xmlname[XMLCONFIG_MAX];
    char xmlfile[PATH_MAX];
    char xmluri[PATH_MAX];
} xmlconfigitem;

#endif
