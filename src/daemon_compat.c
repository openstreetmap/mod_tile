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

#include "config.h"
#ifndef HAVE_DAEMON

#ifdef HAVE_SYS_CDEFS_C
#include <sys/cdefs.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#ifdef HAVE_PATHS_H
#include <paths.h>
#endif
#ifndef _PATH_DEVNULL
#define _PATH_DEVNULL "/dev/null"
#endif

int
daemon(nochdir, noclose)
int nochdir, noclose;
{
	struct sigaction osa, sa;
	int fd;
	pid_t newgrp;
	int oerrno;
	int osa_ok;

	/* A SIGHUP may be thrown when the parent exits below. */
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	osa_ok = sigaction(SIGHUP, &sa, &osa);

	switch (fork()) {
		case -1:
			return (-1);

		case 0:
			break;

		default:
			exit(0);
	}

	newgrp = setsid();
	oerrno = errno;

	if (osa_ok != -1) {
		sigaction(SIGHUP, &osa, NULL);
	}

	if (newgrp == -1) {
		errno = oerrno;
		return (-1);
	}

	if (!nochdir) {
		(void)chdir("/");
	}

	if (!noclose && (fd = open(_PATH_DEVNULL, O_RDWR, 0)) != -1) {
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);

		if (fd > 2) {
			close(fd);
		}
	}

	return (0);
}
#endif
