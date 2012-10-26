AUTOMAKE_OPTIONS = foreign
SUBDIRS = iniparser3.0b

ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = $(FT2_CFLAGS) $(PTHREAD_CFLAGS) $(BOOST_CPPFLAGS) $(ICU_CPPFLAGS)

bin_PROGRAMS = render_old render_expired render_list speedtest render_old renderd convert_meta

renderddir = $(sysconfdir)
renderd_SOURCES = protocol.h store.c daemon.h daemon.c daemon_compat.c gen_tile.h gen_tile.cpp dir_utils.c sys_utils.c sys_utils.h render_config.h dir_utils.h store.h iniparser3.0b/libiniparser.la
renderd_LDADD = $(FT2_LIBS) $(PTHREAD_CFLAGS) $(MAPNIK_LDFLAGS) $(BOOST_LDFLAGS) $(ICU_LDFLAGS) -Liniparser3.0b/.libs -liniparser
renderd_DATA = renderd.conf
speedtest_SOURCES = render_config.h protocol.h dir_utils.c dir_utils.h speedtest.cpp
render_list_SOURCES = render_config.h protocol.h dir_utils.c dir_utils.h render_list.c sys_utils.c sys_utils.h
render_list_LDADD = $(PTHREAD_CFLAGS)
render_expired_SOURCES = render_config.h protocol.h dir_utils.c dir_utils.h render_expired.c
render_expired_LDADD = $(PTHREAD_CFLAGS)
render_old_SOURCES = render_config.h protocol.h dir_utils.c dir_utils.h render_old.c sys_utils.c sys_utils.h
render_old_LDADD = $(PTHREAD_CFLAGS)
convert_meta_SOURCES = render_config.h protocol.h dir_utils.c dir_utils.h store.c convert_meta.c

all-local:
	$(APXS) -c $(DEF_LDLIBS) $(AM_CFLAGS) $(AM_LDFLAGS) @srcdir@/mod_tile.c @srcdir@/store.c @srcdir@/dir_utils.c @srcdir@/sys_utils.c

install-mod_tile: 
	mkdir -p $(DESTDIR)`$(APXS) -q LIBEXECDIR`
	$(APXS) -S LIBEXECDIR=$(DESTDIR)`$(APXS) -q LIBEXECDIR` -c -i $(DEF_LDLIBS) $(AM_CFLAGS) $(AM_LDFLAGS) @srcdir@/mod_tile.c @srcdir@/store.c @srcdir@/dir_utils.c @srcdir@/sys_utils.c


