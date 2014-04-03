#ifndef DAEMONHELPER_H
#define DAEMONHELPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "protocol.h"

int send_cmd(struct protocol * cmd, int fd);
int recv_cmd(struct protocol * cmd, int fd, int block);


#ifdef __cplusplus
}
#endif
#endif
