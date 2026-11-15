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

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Protocol between client and render daemon
 *
 * ver = 2;
 *
 * cmdRender(z,x,y,xmlconfig), response: {cmdDone(z,x,y), cmdBusy(z,x,y)}
 * cmdDirty(z,x,y,xmlconfig), no response
 *
 * A client may not bother waiting for a response if the render daemon is too slow
 * causing responses to get slightly out of step with requests.
 */

/** @brief Maximum filesystem path length for rendered tile/metatile paths. */
#define TILE_PATH_MAX (256)

/** @brief Current wire protocol version exchanged between `mod_tile` clients and `renderd`. */
#define PROTO_VER (3)

#ifndef RENDERD_SOCKET
/** @brief Default UNIX domain socket path used to communicate with `renderd`. */
#define RENDERD_SOCKET "/run/renderd/renderd.sock"
#endif

#ifndef RENDERD_HOST
/** @brief Default TCP hostname used when connecting to `renderd` over the network. */
#define RENDERD_HOST "localhost"
#endif

#ifndef RENDERD_PORT
/** @brief Default TCP port number used when connecting to `renderd` over the network. */
#define RENDERD_PORT 7654
#endif

/** @brief Maximum buffer size (including NUL terminator) for map style names, MIME types, and options. */
#define XMLCONFIG_MAX 41

	/**
	 * @brief Command opcodes exchanged over the `mod_tile` / `renderd` IPC wire protocol.
	 */
	enum protoCmd {
		/** @brief No-op / ignored message. */
		cmdIgnore,
		/** @brief Standard interactive tile render request (expects `cmdDone` or `cmdNotDone` reply). */
		cmdRender,
		/** @brief Mark a metatile as dirty for background re-rendering (no reply expected). */
		cmdDirty,
		/** @brief Response from `renderd` indicating the requested metatile finished rendering. */
		cmdDone,
		/** @brief Response from `renderd` indicating the request was dropped or the daemon is busy. */
		cmdNotDone,
		/** @brief High-priority interactive tile render request (e.g. missing tile on disk). */
		cmdRenderPrio,
		/** @brief Bulk/batch tile render request (e.g. from `render_list`). */
		cmdRenderBulk,
		/** @brief Low-priority interactive tile render request (e.g. expired tile served stale). */
		cmdRenderLow
	};

	/**
	 * @brief Version 3 wire protocol message packet (`PROTO_VER == 3`).
	 *
	 * Extends version 2 with `mimetype` and style parameterization `options`.
	 */
	struct protocol {
		/** @brief Protocol version number (`3` for `struct protocol`). */
		int ver;
		/** @brief Request or response command opcode. */
		enum protoCmd cmd;
		/** @brief Tile X coordinate. */
		int x;
		/** @brief Tile Y coordinate. */
		int y;
		/** @brief Tile zoom level. */
		int z;
		/** @brief Null-terminated target map configuration (`xmlconfig`) name. */
		char xmlname[XMLCONFIG_MAX];
		/** @brief Null-terminated requested output MIME type (e.g. `"image/png"`). */
		char mimetype[XMLCONFIG_MAX];
		/** @brief Null-terminated style parameterization options string. */
		char options[XMLCONFIG_MAX];
	};

	/**
	 * @brief Legacy version 1 wire protocol message packet (`ver == 1`).
	 *
	 * Contains only coordinates and command opcode, predating named map configurations.
	 */
	struct protocol_v1 {
		/** @brief Protocol version number (`1`). */
		int ver;
		/** @brief Request or response command opcode. */
		enum protoCmd cmd;
		/** @brief Tile X coordinate. */
		int x;
		/** @brief Tile Y coordinate. */
		int y;
		/** @brief Tile zoom level. */
		int z;
	};

	/**
	 * @brief Legacy version 2 wire protocol message packet (`ver == 2`).
	 *
	 * Adds the `xmlname` map configuration selector to `struct protocol_v1`.
	 */
	struct protocol_v2 {
		/** @brief Protocol version number (`2`). */
		int ver;
		/** @brief Request or response command opcode. */
		enum protoCmd cmd;
		/** @brief Tile X coordinate. */
		int x;
		/** @brief Tile Y coordinate. */
		int y;
		/** @brief Tile zoom level. */
		int z;
		/** @brief Null-terminated target map configuration (`xmlconfig`) name. */
		char xmlname[XMLCONFIG_MAX];
	};

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_H */
