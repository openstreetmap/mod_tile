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

#include <arpa/inet.h>
#include <glib.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cache_expire.h"
#include "g_logger.h"
/**
 * This function sends a HTCP cache clr request for a given
 * URL.
 * RFC for HTCP can be found at http://www.htcp.org/
 */
static void cache_expire_url(int sock, char *url)
{
	char *buf;

	if (sock < 0) {
		return;
	}

	int idx = 0;
	int url_len;

	url_len = strlen(url);
	buf = (char *)malloc(12 + 22 + url_len);

	if (!buf) {
		return;
	}

	idx = 0;

	// 16 bit: Overall length of the datagram packet, including this header
	*((uint16_t *)(&buf[idx])) = htons(12 + 22 + url_len);
	idx += 2;

	// HTCP version. Currently at 0.0
	buf[idx++] = 0; // Major version
	buf[idx++] = 0; // Minor version

	// Length of HTCP data, including this field
	*((uint16_t *)(&buf[idx])) = htons(8 + 22 + url_len);
	idx += 2;

	// HTCP opcode CLR=4
	buf[idx++] = 4;
	// Reserved
	buf[idx++] = 0;

	// 32 bit transaction id;
	*((uint32_t *)(&buf[idx])) = htonl(255);
	idx += 4;

	buf[idx++] = 0;
	buf[idx++] = 0; // HTCP reason

	// Length of the Method string
	*((uint16_t *)(&buf[idx])) = htons(4);
	idx += 2;

	/// Method string
	memcpy(&buf[idx], "HEAD", 4);
	idx += 4;

	// Length of the url string
	*((uint16_t *)(&buf[idx])) = htons(url_len);
	idx += 2;

	// Url string
	memcpy(&buf[idx], url, url_len);
	idx += url_len;

	// Length of version string
	*((uint16_t *)(&buf[idx])) = htons(8);
	idx += 2;

	// version string
	memcpy(&buf[idx], "HTTP/1.1", 8);
	idx += 8;

	// Length of request headers. Currently 0 as we don't have any headers to send
	*((uint16_t *)(&buf[idx])) = htons(0);

	if (send(sock, (void *)buf, (12 + 22 + url_len), 0) < (12 + 22 + url_len)) {
		g_logger(G_LOG_LEVEL_ERROR, "Failed to send HTCP purge for %s", url);
	};

	free(buf);
}

void cache_expire(int sock, const char *host, const char *uri, int x, int y, int z)
{

	if (sock < 0) {
		return;
	}

	char *url = (char *)malloc(1024);
	snprintf(url, 1024, "http://%s%s%i/%i/%i.png", host, uri, z, x, y);
	cache_expire_url(sock, url);
	free(url);
}

int init_cache_expire(const char *htcphost)
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int sfd, s;

	/* Obtain address(es) matching host/port */

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;	/* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
	hints.ai_flags = 0;
	hints.ai_protocol = 0; /* Any protocol */

	s = getaddrinfo(htcphost, HTCP_EXPIRE_CACHE_PORT, &hints, &result);

	if (s != 0) {
		g_logger(G_LOG_LEVEL_ERROR, "Failed to lookup HTCP cache host: %s", gai_strerror(s));
		return -1;
	}

	/* getaddrinfo() returns a list of address structures.
	 Try each address until we successfully connect(2).
	 If socket(2) (or connect(2)) fails, we (close the socket
	 and) try the next address. */

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

		if (sfd == -1) {
			continue;
		}

		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
			break; /* Success */
		}

		close(sfd);
	}

	if (rp == NULL) { /* No address succeeded */
		g_logger(G_LOG_LEVEL_ERROR, "Failed to create HTCP cache socket");
		return -1;
	}

	freeaddrinfo(result); /* No longer needed */

	return sfd;
}
