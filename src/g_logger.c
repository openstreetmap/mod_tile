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

#define _GNU_SOURCE 1
#define G_LOG_USE_STRUCTURED 1

#include <glib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

/**
 * @brief Execution mode flag indicating whether the daemon runs in the foreground.
 *
 * When set to 1, log output is directed to standard streams (stdout/stderr)
 * via GLib writers rather than systemd-journald or syslog.
 */
int foreground = 0;

/**
 * @brief Convert a GLib log level flag into a human-readable uppercase label.
 *
 * @param log_level GLib log level (e.g., G_LOG_LEVEL_ERROR, G_LOG_LEVEL_INFO).
 * @return Constant string representation of the log level, or "UNKNOWN" if
 *         the level is not recognized.
 */
const char *g_logger_level_name(int log_level)
{
	switch (log_level) {
	case G_LOG_LEVEL_ERROR:
		return "ERROR";

	case G_LOG_LEVEL_CRITICAL:
		return "CRITICAL";

	case G_LOG_LEVEL_WARNING:
		return "WARNING";

	case G_LOG_LEVEL_MESSAGE:
		return "MESSAGE";

	case G_LOG_LEVEL_INFO:
		return "INFO";

	case G_LOG_LEVEL_DEBUG:
		return "DEBUG";

	default:
		return "UNKNOWN";
	}
}

/**
 * @brief Format and route a log message to stdout/stderr, systemd-journald, or syslog.
 *
 * Selects the logging backend automatically based on runtime environment:
 *   1. Standard streams (`stdout`/`stderr`) when `foreground == 1`.
 *   2. `systemd-journald` when `stderr` is connected to the journal socket.
 *   3. Traditional `syslog` (up to `LOG_INFO` severity) as a fallback.
 *
 * Unlike default GLib fatal error handling, `G_LOG_LEVEL_ERROR` messages are
 * logged without terminating the process, and `G_LOG_LEVEL_INFO` messages are
 * emitted unconditionally without requiring `G_MESSAGES_DEBUG`.
 *
 * @param log_level GLib log severity level (`GLogLevelFlags`).
 * @param format    `printf`-style format string.
 * @param ...       Variadic arguments corresponding to `format`.
 */
void g_logger(int log_level, const char *format, ...)
{
	/* Byte length returned by vasprintf()/asprintf(), or -1 on allocation failure */
	int size;
	/* Heap-allocated formatted message body and level-prefixed syslog string */
	char *log_message, *log_message_prefixed;
	/* Variadic argument list for printf-style formatting */
	va_list args;

	va_start(args, format);

	size = vasprintf(&log_message, format, args);

	if (size == -1) {
		g_error("ERROR: vasprintf failed in g_logger");
	}

	/* Structured GLib log field array (length -1 indicates null-terminated string) */
	const GLogField log_fields[] = {{"MESSAGE", log_message, -1}};

	size = asprintf(&log_message_prefixed, "%s: %s", g_logger_level_name(log_level), log_message);

	if (size == -1) {
		g_error("ERROR: asprintf failed in g_logger");
	}

	if (foreground == 1) {
		switch (log_level) {
		/* Bypass g_log_writer_default() so G_LOG_LEVEL_ERROR does not abort the process */
		case G_LOG_LEVEL_ERROR:
			g_log_writer_standard_streams((GLogLevelFlags)log_level, log_fields, 1, NULL);
			break;

		/* Bypass g_log_writer_default() so G_LOG_LEVEL_INFO prints even without G_MESSAGES_DEBUG */
		case G_LOG_LEVEL_INFO:
			g_log_writer_standard_streams((GLogLevelFlags)log_level, log_fields, 1, NULL);
			break;

		default:
			g_log_writer_default((GLogLevelFlags)log_level, log_fields, 1, NULL);
		}
	} else if (g_log_writer_is_journald(fileno(stderr))) {
		switch (log_level) {
		/* Bypass g_log_writer_default() so G_LOG_LEVEL_ERROR does not abort the process */
		case G_LOG_LEVEL_ERROR:
			g_log_writer_journald((GLogLevelFlags)log_level, log_fields, 1, NULL);
			break;

		/* Bypass g_log_writer_default() so G_LOG_LEVEL_INFO is sent even without G_MESSAGES_DEBUG */
		case G_LOG_LEVEL_INFO:
			g_log_writer_journald((GLogLevelFlags)log_level, log_fields, 1, NULL);
			break;

		default:
			g_log_writer_default((GLogLevelFlags)log_level, log_fields, 1, NULL);
		}
	} else {
		/* Allow priorities from LOG_EMERG (0) through LOG_INFO (6), filtering out LOG_DEBUG (7) */
		setlogmask(LOG_UPTO(LOG_INFO));

		switch (log_level) {
		case G_LOG_LEVEL_ERROR:
			syslog(LOG_ERR, "%s", log_message_prefixed);
			break;

		case G_LOG_LEVEL_CRITICAL:
			syslog(LOG_CRIT, "%s", log_message_prefixed);
			break;

		case G_LOG_LEVEL_WARNING:
			syslog(LOG_WARNING, "%s", log_message_prefixed);
			break;

		case G_LOG_LEVEL_MESSAGE:
			syslog(LOG_INFO, "%s", log_message_prefixed);
			break;

		case G_LOG_LEVEL_INFO:
			syslog(LOG_INFO, "%s", log_message_prefixed);
			break;

		case G_LOG_LEVEL_DEBUG:
			syslog(LOG_DEBUG, "%s", log_message_prefixed);
			break;
		}
	}

	va_end(args);

	free(log_message_prefixed);
	free(log_message);
}
