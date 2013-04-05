# Copyright © 2013 mod_tile contributors
#
# This file is part of mod_tile.
#
# mod_tile is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 2 of the License, or (at your
# option) any later version.
#
# mod_tile is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with mod_tile.  If not, see <http://www.gnu.org/licenses/>.

AUTOMAKE_OPTIONS = foreign
SUBDIRS = iniparser3.0b

ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = $(FT2_CFLAGS) $(PTHREAD_CFLAGS) $(BOOST_CPPFLAGS) $(ICU_CPPFLAGS)

STORE_SOURCES = store.c store_file.c store_file_utils.c store_memcached.c store_rados.c store_ro_http_proxy.c store_ro_composite.c
STORE_LDFLAGS = $(LIBMEMCACHED_LDFLAGS) $(LIBRADOS_LDFLAGS) $(LIBCURL) $(CAIRO_LIBS)

bin_PROGRAMS = renderd render_expired render_list render_speedtest render_old
man_MANS = docs/renderd.8 docs/render_expired.1 docs/render_list.1 docs/render_old.1 docs/render_speedtest.1

renderddir = $(sysconfdir)
renderd_SOURCES = protocol.h daemon.h daemon.c daemon_compat.c gen_tile.h gen_tile.cpp sys_utils.c sys_utils.h render_config.h $(STORE_SOURCES) iniparser3.0b/libiniparser.la 
renderd_LDADD = $(FT2_LIBS) $(PTHREAD_CFLAGS) $(MAPNIK_LDFLAGS) $(BOOST_LDFLAGS) $(ICU_LDFLAGS) $(STORE_LDFLAGS) -Liniparser3.0b/.libs -liniparser
renderd_DATA = renderd.conf
render_speedtest_SOURCES = render_config.h protocol.h speedtest.cpp render_submit_queue.c sys_utils.c
render_speedtest_LDADD = $(PTHREAD_CFLAGS)
render_list_SOURCES = render_config.h protocol.h render_list.c sys_utils.c sys_utils.h render_submit_queue.c $(STORE_SOURCES)
render_list_LDADD = $(PTHREAD_CFLAGS) $(STORE_LDFLAGS)
render_expired_SOURCES = render_config.h protocol.h render_expired.c render_submit_queue.c sys_utils.c $(STORE_SOURCES)
render_expired_LDADD = $(PTHREAD_CFLAGS) $(STORE_LDFLAGS)
render_old_SOURCES = render_config.h protocol.h store_file_utils.c render_old.c sys_utils.c sys_utils.h render_submit_queue.c
render_old_LDADD = $(PTHREAD_CFLAGS)
#convert_meta_SOURCES = render_config.h protocol.h dir_utils.c dir_utils.h store.c convert_meta.c

all-local:
	$(APXS) -c $(DEF_LDLIBS) $(AM_CFLAGS) $(AM_LDFLAGS) $(STORE_LDFLAGS) @srcdir@/mod_tile.c  @srcdir@/sys_utils.c @srcdir@/store.c @srcdir@/store_file.c @srcdir@/store_file_utils.c @srcdir@/store_memcached.c @srcdir@/store_rados.c @srcdir@/store_ro_http_proxy.c @srcdir@/store_ro_composite.c

install-mod_tile: 
	mkdir -p $(DESTDIR)`$(APXS) -q LIBEXECDIR`
	$(APXS) -S LIBEXECDIR=$(DESTDIR)`$(APXS) -q LIBEXECDIR` -c -i $(DEF_LDLIBS) $(AM_CFLAGS) $(AM_LDFLAGS) $(STORE_LDFLAGS) @srcdir@/mod_tile.c @srcdir@/sys_utils.c @srcdir@/store.c @srcdir@/store_file.c @srcdir@/store_file_utils.c @srcdir@/store_memcached.c @srcdir@/store_rados.c @srcdir@/store_ro_http_proxy.c @srcdir@/store_ro_composite.c  


