/*
 * Copyright (c) 2007 - 2020 by mod_tile contributors (see AUTHORS file)
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
#include <stdio.h>
#include <syslog.h>

extern int log_to_std_streams;

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

void g_logger(int log_level, const char *format, ...)
{
	int size;
	char *log_message, *log_message_prefixed;

	va_list args;

	va_start(args, format);

	size = vasprintf(&log_message, format, args);

	if (size == -1) {
		g_error("ERROR: vasprintf failed in g_logger");
	}

	const GLogField log_fields[] = {{"MESSAGE", log_message, -1}};

	size = asprintf(&log_message_prefixed, "%s: %s", g_logger_level_name(log_level), log_message);

	if (size == -1) {
		g_error("ERROR: asprintf failed in g_logger");
	}

	const GLogField log_fields_prefixed[] = {{"MESSAGE", log_message_prefixed, -1}};

	if (log_to_std_streams == 1) {
		switch (log_level) {
			// Levels >= G_LOG_LEVEL_ERROR will terminate the program
			case G_LOG_LEVEL_ERROR:
				g_log_writer_standard_streams(log_level, log_fields, 1, NULL);
				break;

			// Levels <= G_LOG_LEVEL_INFO will only show when using G_MESSAGES_DEBUG
			case G_LOG_LEVEL_INFO:
				g_log_writer_standard_streams(log_level, log_fields, 1, NULL);
				break;

			default:
				g_log_writer_default(log_level, log_fields, 1, NULL);
		}
	} else if (g_log_writer_is_journald(fileno(stderr))) {
		switch (log_level) {
			// Levels >= G_LOG_LEVEL_ERROR will terminate the program
			case G_LOG_LEVEL_ERROR:
				g_log_writer_journald(log_level, log_fields, 1, NULL);
				break;

			// Levels <= G_LOG_LEVEL_INFO will only show when using G_MESSAGES_DEBUG
			case G_LOG_LEVEL_INFO:
				g_log_writer_journald(log_level, log_fields, 1, NULL);
				break;

			default:
				g_log_writer_default(log_level, log_fields, 1, NULL);
		}
	} else {
		setlogmask(LOG_UPTO(LOG_INFO));

		switch (log_level) {
			case G_LOG_LEVEL_ERROR:
				syslog(LOG_ERR, log_message_prefixed, NULL);
				break;

			case G_LOG_LEVEL_CRITICAL:
				syslog(LOG_CRIT, log_message_prefixed, NULL);
				break;

			case G_LOG_LEVEL_WARNING:
				syslog(LOG_WARNING, log_message_prefixed, NULL);
				break;

			case G_LOG_LEVEL_MESSAGE:
				syslog(LOG_INFO, log_message_prefixed, NULL);
				break;

			case G_LOG_LEVEL_INFO:
				syslog(LOG_INFO, log_message_prefixed, NULL);
				break;

			case G_LOG_LEVEL_DEBUG:
				syslog(LOG_DEBUG, log_message_prefixed, NULL);
				break;
		}
	}

	va_end(args);

	free(log_message_prefixed);
	free(log_message);
}
