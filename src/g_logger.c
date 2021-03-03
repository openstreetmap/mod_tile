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

#include <glib.h>
#include <stdio.h>
#include <syslog.h>

extern int foreground;

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
	}
}

void g_logger(int log_level, char *format, ...)
{
	char *log_format;

	va_list args;

	va_start(args, format);

	asprintf(&log_format, "%s: %s", g_logger_level_name(log_level), format);

	if (foreground == 1) {
		switch (log_level) {
			// Levels >= G_LOG_LEVEL_ERROR will terminate the program
			case G_LOG_LEVEL_ERROR:
				g_logv(G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL, log_format, args);
				break;

			// Levels <= G_LOG_LEVEL_INFO will only show when using G_MESSAGES_DEBUG
			case G_LOG_LEVEL_INFO:
				g_logv(G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, log_format, args);
				break;

			default:
				g_logv(G_LOG_DOMAIN, log_level, log_format, args);
		}
	} else {
		switch (log_level) {
			case G_LOG_LEVEL_ERROR:
				vsyslog(LOG_ERR, log_format, args);
				break;

			case G_LOG_LEVEL_CRITICAL:
				vsyslog(LOG_CRIT, log_format, args);
				break;

			case G_LOG_LEVEL_WARNING:
				vsyslog(LOG_WARNING, log_format, args);
				break;

			case G_LOG_LEVEL_MESSAGE:
				vsyslog(LOG_INFO, log_format, args);
				break;

			case G_LOG_LEVEL_INFO:
				vsyslog(LOG_INFO, log_format, args);
				break;

			case G_LOG_LEVEL_DEBUG:
				vsyslog(LOG_DEBUG, log_format, args);
				break;
		}
	}

	free(log_format);

	va_end(args);
}
