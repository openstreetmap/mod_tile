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

#include <errno.h>
#include <glib.h>
#include <string.h>
#include <sys/socket.h>

#include "g_logger.h"
#include "protocol.h"

/**
 * @brief Serialize and transmit a renderd protocol command message over a socket.
 *
 * Validates the file descriptor and protocol version (`1`, `2`, or `3`), selects
 * the wire structure size corresponding to `cmd->ver` (`protocol_v1`, `protocol_v2`,
 * or full `protocol` v3), and sends the exact byte sequence via `send()`.
 *
 * @param cmd Pointer to the populated protocol command structure to transmit.
 * @param fd  Connected socket file descriptor.
 * @return Number of bytes sent (matching the version's struct size) on success,
 *         or `-1` (or a short byte count) on validation or socket error.
 */
int send_cmd(struct protocol *cmd, int fd)
{
	/* Byte count returned by send(), or -1 on error */
	int ret = -1;
	/* Dummy output buffer used with getsockopt(SO_TYPE) to verify fd is a socket */
	int socket_type;
	/* Wire byte length corresponding to cmd->ver */
	size_t expected_len = 0;
	/* Option length parameter passed to getsockopt() */
	socklen_t optlen = sizeof(socket_type);

	if (fd < 0) {
		g_logger(G_LOG_LEVEL_WARNING, "Attempted to send cmd on invalid fd %d", fd);
		return -1;
	}

	g_logger(G_LOG_LEVEL_DEBUG, "Sending render cmd(%i %s %i/%i/%i) with protocol version %i to fd %i", cmd->cmd, cmd->xmlname, cmd->z, cmd->x, cmd->y, cmd->ver, fd);

	if ((cmd->ver > 3) || (cmd->ver < 1)) {
		g_logger(G_LOG_LEVEL_ERROR, "Failed to send render cmd with unknown protocol version %i on fd %d", cmd->ver, fd);
		return -1;
	}

	/* Verify that fd refers to a valid open socket before calling send() */
	if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &socket_type, &optlen) < 0) {
		g_logger(G_LOG_LEVEL_ERROR, "fd %d is not a valid socket: %s", fd, strerror(errno));
		return -1;
	}

	switch (cmd->ver) {
	case 1:
		expected_len = sizeof(struct protocol_v1);
		break;

	case 2:
		expected_len = sizeof(struct protocol_v2);
		break;

	case 3:
		expected_len = sizeof(struct protocol);
		break;
	}

	ret = send(fd, cmd, expected_len, 0);

	if (ret < 0 || (size_t)ret != expected_len) {
		g_logger(G_LOG_LEVEL_ERROR, "Failed to send render cmd on fd %i", fd);
		g_logger(G_LOG_LEVEL_ERROR, "send error: %s", strerror(errno));
	}

	return ret;
}

/**
 * @brief Receive a renderd protocol command message from a socket.
 *
 * Performs a two-stage read to support backwards-compatible protocol versions:
 *   1. Reads the common `struct protocol_v1` header prefix to determine `cmd->ver`.
 *   2. For protocol versions `2` and `3`, reads the remaining version-specific
 *      trailing fields directly into `cmd` immediately after the v1 prefix.
 *
 * @param cmd   Destination `struct protocol` buffer (zero-initialized before reading).
 * @param fd    Connected socket file descriptor to read from.
 * @param block Non-zero to block until the full message arrives (`MSG_WAITALL`),
 *              or `0` for non-blocking I/O (`MSG_DONTWAIT`).
 * @return Total bytes read (`sizeof(protocol_v1)`, `sizeof(protocol_v2)`, or
 *         `sizeof(protocol)`) on success; `0` if an incomplete message was read;
 *         `-1` on EOF, socket error, premature close, or unknown protocol version.
 */
int recv_cmd(struct protocol *cmd, int fd, int block)
{
	/* Byte count from the initial v1 header recv() (and later combined total bytes read) */
	int ret;
	/* Byte count from the secondary recv() for v2/v3 extension fields */
	int ret2 = 0;

	memset(cmd, 0, sizeof(*cmd));

	/* Stage 1: Read the base protocol_v1 header to inspect cmd->ver */
	ret = recv(fd, cmd, sizeof(struct protocol_v1), block ? MSG_WAITALL : MSG_DONTWAIT);

	if (ret < 1) {
		g_logger(G_LOG_LEVEL_DEBUG, "Failed to read cmd on fd %i", fd);
		return -1;
	} else if (ret < (int)sizeof(struct protocol_v1)) {
		g_logger(G_LOG_LEVEL_DEBUG, "Read incomplete cmd on fd %i", fd);
		return 0;
	}

	if ((cmd->ver > 3) || (cmd->ver < 1)) {
		g_logger(G_LOG_LEVEL_WARNING, "Failed to receive render cmd with unknown protocol version %i", cmd->ver);
		return -1;
	}

	g_logger(G_LOG_LEVEL_DEBUG, "Got incoming request with protocol version %i", cmd->ver);

	/* Stage 2: Read version-specific trailing fields following the v1 prefix */
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
