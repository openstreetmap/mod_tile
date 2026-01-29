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

#include "protocol.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "g_logger.h"

int send_cmd(struct protocol * cmd, int fd)
{
	int ret;
	g_logger(G_LOG_LEVEL_DEBUG, "Sending render cmd(%i %s %i/%i/%i) with protocol version %i to fd %i", cmd->cmd, cmd->xmlname, cmd->z, cmd->x, cmd->y, cmd->ver, fd);

	if ((cmd->ver > 3) || (cmd->ver < 1)) {
		g_logger(G_LOG_LEVEL_ERROR, "Failed to send render cmd with unknown protocol version %i on fd %d", cmd->ver, fd);
		return -1;
	}

	switch (cmd->ver) {
		case 1:
			ret = send(fd, cmd, sizeof(struct protocol_v1), 0);
			break;

		case 2:
			ret = send(fd, cmd, sizeof(struct protocol_v2), 0);
			break;

		case 3:
			ret = send(fd, cmd, sizeof(struct protocol), 0);
			break;
	}

	if ((ret != sizeof(struct protocol)) && (ret != sizeof(struct protocol_v2)) && (ret != sizeof(struct protocol_v1))) {
		g_logger(G_LOG_LEVEL_ERROR, "Failed to send render cmd on fd %i", fd);
		g_logger(G_LOG_LEVEL_ERROR, "send error: %s", strerror(errno));
	}

	return ret;
}

int recv_cmd(struct protocol * cmd, int fd,  int block)
{
	int ret, ret2;
	memset(cmd, 0, sizeof(*cmd));
	ret = recv(fd, cmd, sizeof(struct protocol_v1), block ? MSG_WAITALL : MSG_DONTWAIT);

	if (ret < 1) {
		g_logger(G_LOG_LEVEL_DEBUG, "Failed to read cmd on fd %i", fd);
		return -1;
	} else if (ret < sizeof(struct protocol_v1)) {
		g_logger(G_LOG_LEVEL_DEBUG, "Read incomplete cmd on fd %i", fd);
		return 0;
	}

	if ((cmd->ver > 3) || (cmd->ver < 1)) {
		g_logger(G_LOG_LEVEL_WARNING, "Failed to receive render cmd with unknown protocol version %i", cmd->ver);
		return -1;
	}

	g_logger(G_LOG_LEVEL_DEBUG, "Got incoming request with protocol version %i", cmd->ver);

	switch (cmd->ver) {
		case 1:
			ret2 = 0;
			break;

		case 2:
			ret2 = recv(fd, ((char *)cmd) + sizeof(struct protocol_v1), sizeof(struct protocol_v2) - sizeof(struct protocol_v1), block ? MSG_WAITALL : MSG_DONTWAIT);
			break;

		case 3:
			ret2 = recv(fd, ((char *)cmd) + sizeof(struct protocol_v1), sizeof(struct protocol) - sizeof(struct protocol_v1), block ? MSG_WAITALL : MSG_DONTWAIT);
			break;
	}

	if ((cmd->ver > 1) && (ret2 < 1)) {
		g_logger(G_LOG_LEVEL_WARNING, "Socket prematurely closed: %i", fd);
		return -1;
	}

	ret += ret2;

	if ((ret == sizeof(struct protocol)) || (ret == sizeof(struct protocol_v1)) || (ret == sizeof(struct protocol_v2))) {
		return ret;
	}

	g_logger(G_LOG_LEVEL_WARNING, "Socket read wrong number of bytes: %i -> %li, %li", ret, sizeof(struct protocol_v2), sizeof(struct protocol));
	return 0;
}
