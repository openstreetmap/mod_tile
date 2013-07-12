AUTOMAKE_OPTIONS = foreign
SUBDIRS = iniparser3.0b

ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = $(FT2_CFLAGS) $(PTHREAD_CFLAGS) $(BOOST_CPPFLAGS) $(ICU_CPPFLAGS) $(CAIRO_CFLAGS) $(CAIROMM_CFLAGS)

STORE_SOURCES = store.c store_file.c store_file_utils.c store_memcached.c store_rados.c store_ro_http_proxy.c store_ro_composite.c store_null.c
STORE_LDFLAGS = $(LIBMEMCACHED_LDFLAGS) $(LIBRADOS_LDFLAGS) $(LIBCURL) $(CAIRO_LIBS)

bin_PROGRAMS = renderd render_expired render_list render_speedtest render_old
noinst_PROGRAMS = gen_tile_test
man_MANS = docs/renderd.8 docs/render_expired.1 docs/render_list.1 docs/render_old.1 docs/render_speedtest.1

renderddir = $(sysconfdir)
renderd_SOURCES = protocol.h daemon.h daemon.c daemon_compat.c gen_tile.h gen_tile.cpp sys_utils.c sys_utils.h render_config.h request_queue.c cache_expire.c metatile.cpp $(STORE_SOURCES) iniparser3.0b/libiniparser.la 
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
gen_tile_test_SOURCES = gen_tile_test.cpp metatile.cpp request_queue.c catch.hpp protocol.h daemon.h daemon.c daemon_compat.c gen_tile.h gen_tile.cpp sys_utils.c cache_expire.c sys_utils.h render_config.h $(STORE_SOURCES) iniparser3.0b/libiniparser.la 
gen_tile_test_CFLAGS = -DMAIN_ALREADY_DEFINED $(PTHREAD_CFLAGS)
gen_tile_test_LDADD = $(FT2_LIBS) $(MAPNIK_LDFLAGS) $(BOOST_LDFLAGS) $(ICU_LDFLAGS) $(STORE_LDFLAGS) -Liniparser3.0b/.libs -liniparser

CLEANFILES=*.slo mod_tile.la stderr.out

test: gen_tile_test
	./gen_tile_test

all-local:
	$(APXS) -c $(DEF_LDLIBS) $(AM_CFLAGS) $(AM_LDFLAGS) $(STORE_LDFLAGS) @srcdir@/mod_tile.c  @srcdir@/sys_utils.c @srcdir@/store.c @srcdir@/store_file.c @srcdir@/store_file_utils.c @srcdir@/store_memcached.c @srcdir@/store_rados.c @srcdir@/store_ro_http_proxy.c @srcdir@/store_ro_composite.c @srcdir@/store_null.c

install-mod_tile: 
	mkdir -p $(DESTDIR)`$(APXS) -q LIBEXECDIR`
	$(APXS) -S LIBEXECDIR=$(DESTDIR)`$(APXS) -q LIBEXECDIR` -c -i $(DEF_LDLIBS) $(AM_CFLAGS) $(AM_LDFLAGS) $(STORE_LDFLAGS) @srcdir@/mod_tile.c @srcdir@/sys_utils.c @srcdir@/store.c @srcdir@/store_file.c @srcdir@/store_file_utils.c @srcdir@/store_memcached.c @srcdir@/store_rados.c @srcdir@/store_ro_http_proxy.c @srcdir@/store_ro_composite.c @srcdir@/store_null.c


