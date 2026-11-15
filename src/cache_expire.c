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
 * @brief Write a 16-bit unsigned integer in network byte order (big-endian)
 *        at an arbitrary (possibly unaligned) memory address.
 *
 * Casting &buf[idx] directly to uint16_t* is undefined behavior in C and
 * can cause alignment faults on strict-alignment architectures.
 *
 * @param dst   Destination buffer pointer.
 * @param value 16-bit value in host byte order.
 */
static void put_u16(char *dst, uint16_t value)
{
	/* Value converted to big-endian network byte order */
	uint16_t net = htons(value);
	memcpy(dst, &net, sizeof(net));
}

/**
 * @brief Write a 32-bit unsigned integer in network byte order (big-endian)
 *        at an arbitrary (possibly unaligned) memory address.
 *
 * Casting &buf[idx] directly to uint32_t* is undefined behavior in C and
 * can cause alignment faults on strict-alignment architectures.
 *
 * @param dst   Destination buffer pointer.
 * @param value 32-bit value in host byte order.
 */
static void put_u32(char *dst, uint32_t value)
{
	/* Value converted to big-endian network byte order */
	uint32_t net = htonl(value);
	memcpy(dst, &net, sizeof(net));
}

/**
 * @brief Construct and send an HTCP (Hyper Text Caching Protocol) CLR
 *        (cache clear/purge) UDP datagram for a given URL.
 *
 * Follows RFC 2756 packet framing:
 *   - 12 bytes: HTCP outer header (length, version, data length, opcode,
 *               reserved, transaction ID) + 2-byte auth trailer length (0)
 *   - 22 bytes: CLR specifier fixed fields (reason, method length + "HEAD",
 *               URL length, version length + "HTTP/1.1", headers length)
 *   - url_len bytes: Target URL string
 *
 * @param sock Connected UDP socket descriptor to the HTCP cache server.
 * @param url  Null-terminated URL string to purge from cache.
 */
static void cache_expire_url(int sock, char *url)
{
	/* Dynamically allocated buffer holding the raw HTCP UDP packet */
	char *buf;
	/* Current byte write offset within buf */
	int idx = 0;
	/* Byte length of the target URL (excluding null terminator) */
	int url_len;

	if (sock < 0) {
		return;
	}

	url_len = strlen(url);
	buf = (char *)malloc(12 + 22 + url_len);

	if (!buf) {
		return;
	}

	idx = 0;

	/* 16-bit: Overall length of the datagram packet, including this header */
	put_u16(&buf[idx], 12 + 22 + url_len);
	idx += 2;

	/* HTCP version (0.0 per RFC 2756) */
	buf[idx++] = 0; /* Major version */
	buf[idx++] = 0; /* Minor version */

	/* 16-bit: Length of HTCP data section, including this length field */
	put_u16(&buf[idx], 8 + 22 + url_len);
	idx += 2;

	/* HTCP opcode: CLR = 4 (purge request) */
	buf[idx++] = 4;
	/* Reserved byte (must be 0) */
	buf[idx++] = 0;

	/* 32-bit: Transaction ID (hardcoded to 255) */
	put_u32(&buf[idx], 255);
	idx += 4;

	/* 16-bit: HTCP reason field (0 = default/unspecified) */
	buf[idx++] = 0;
	buf[idx++] = 0;

	/* 16-bit: Length of the HTTP method string */
	put_u16(&buf[idx], 4);
	idx += 2;

	/* HTTP method string */
	memcpy(&buf[idx], "HEAD", 4);
	idx += 4;

	/* 16-bit: Length of the target URL string */
	put_u16(&buf[idx], url_len);
	idx += 2;

	/* Target URL string */
	memcpy(&buf[idx], url, url_len);
	idx += url_len;

	/* 16-bit: Length of the HTTP version string */
	put_u16(&buf[idx], 8);
	idx += 2;

	/* HTTP version string */
	memcpy(&buf[idx], "HTTP/1.1", 8);
	idx += 8;

	/* 16-bit: Length of request headers (0 as no headers are sent) */
	put_u16(&buf[idx], 0);

	if (send(sock, (void *)buf, (12 + 22 + url_len), 0) < (12 + 22 + url_len)) {
		g_logger(G_LOG_LEVEL_ERROR, "Failed to send HTCP purge for %s", url);
	}

	free(buf);
}

/**
 * @brief Format a tile URL from Z/X/Y coordinates and send an HTCP purge request.
 *
 * Constructs a URL of the form `http://<host><uri><z>/<x>/<y>.png` (assuming
 * `uri` includes leading and trailing slashes) and passes it to
 * cache_expire_url().
 *
 * @param sock Connected UDP socket descriptor returned by init_cache_expire().
 *             If negative, the function returns immediately without action.
 * @param host Target cache hostname or IP address (e.g., "tile.openstreetmap.org").
 * @param uri  Base URI path prefix for the tileset (e.g., "/").
 * @param x    Tile X coordinate.
 * @param y    Tile Y coordinate.
 * @param z    Tile zoom level.
 */
void cache_expire(int sock, const char *host, const char *uri, int x, int y, int z)
{
	if (sock < 0) {
		return;
	}

	/* Temporary buffer for the formatted tile URL (1024 bytes max) */
	char *url = (char *)malloc(1024);
	if (!url) {
		return;
	}

	snprintf(url, 1024, "http://%s%s%i/%i/%i.png", host, uri, z, x, y);
	cache_expire_url(sock, url);
	free(url);
}

/**
 * @brief Initialize and connect a UDP socket to an HTCP caching server.
 *
 * Resolves `htcphost` using `HTCP_EXPIRE_CACHE_PORT` and attempts to create
 * and connect a `SOCK_DGRAM` socket against each resolved address until one
 * succeeds.
 *
 * @param htcphost Hostname or IP address string of the HTCP cache server.
 * @return Connected socket file descriptor on success, or -1 on failure.
 */
int init_cache_expire(const char *htcphost)
{
	/* Criteria for filtering getaddrinfo() socket address structures */
	struct addrinfo hints;
	/* Linked list head of resolved addresses and current iterator pointer */
	struct addrinfo *result, *rp;
	/* Socket file descriptor (-1 until successfully connected) */
	int sfd = -1;
	/* Return status code from getaddrinfo() */
	int s;

	/* Configure hints for an IPv4 or IPv6 UDP datagram socket */
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

	/*
	 * getaddrinfo() returns a linked list of address structures.
	 * Try each address until we successfully connect(2). If socket(2) or
	 * connect(2) fails, close the socket and try the next address.
	 */
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
		freeaddrinfo(result);
		return -1;
	}

	freeaddrinfo(result); /* Free linked list allocated by getaddrinfo() */

	return sfd;
}
