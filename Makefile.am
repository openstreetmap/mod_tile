AUTOMAKE_OPTIONS = foreign
SUBDIRS = iniparser3.0b

ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = $(FT2_CFLAGS) $(PTHREAD_CFLAGS) $(MAPNIK_INCLUDES) $(BOOST_CPPFLAGS) $(ICU_CPPFLAGS) $(CAIRO_CFLAGS) $(CAIROMM_CFLAGS) 

STORE_SOURCES = src/store.c src/store_file.c src/store_file_utils.c src/store_memcached.c src/store_rados.c src/store_ro_http_proxy.c src/store_ro_composite.c src/store_null.c
STORE_LDFLAGS = $(LIBMEMCACHED_LDFLAGS) $(LIBRADOS_LDFLAGS) $(LIBCURL) $(CAIRO_LIBS)

bin_PROGRAMS = renderd render_expired render_list render_speedtest render_old
noinst_PROGRAMS = gen_tile_test
man_MANS = docs/renderd.8 docs/render_expired.1 docs/render_list.1 docs/render_old.1 docs/render_speedtest.1

renderddir = $(sysconfdir)
renderd_SOURCES = src/daemon.c src/daemon_compat.c src/gen_tile.cpp src/sys_utils.c src/request_queue.c src/cache_expire.c src/metatile.cpp $(STORE_SOURCES) iniparser3.0b/libiniparser.la 
renderd_LDADD = $(FT2_LIBS) $(PTHREAD_CFLAGS) $(MAPNIK_LDFLAGS) $(BOOST_LDFLAGS) $(ICU_LDFLAGS) $(STORE_LDFLAGS) -Liniparser3.0b/.libs -liniparser
renderd_DATA = renderd.conf
render_speedtest_SOURCES = src/speedtest.cpp src/render_submit_queue.c src/sys_utils.c
render_speedtest_LDADD = $(PTHREAD_CFLAGS)
render_list_SOURCES = src/render_list.c src/sys_utils.c src/render_submit_queue.c $(STORE_SOURCES)
render_list_LDADD = $(PTHREAD_CFLAGS) $(STORE_LDFLAGS)
render_expired_SOURCES = src/render_expired.c src/render_submit_queue.c src/sys_utils.c $(STORE_SOURCES)
render_expired_LDADD = $(PTHREAD_CFLAGS) $(STORE_LDFLAGS)
render_old_SOURCES = src/store_file_utils.c src/render_old.c src/sys_utils.c src/render_submit_queue.c
render_old_LDADD = $(PTHREAD_CFLAGS)
#convert_meta_SOURCES = src/dir_utils.c src/store.c src/convert_meta.c
gen_tile_test_SOURCES = src/gen_tile_test.cpp src/metatile.cpp src/request_queue.c src/daemon.c src/daemon_compat.c src/gen_tile.cpp src/sys_utils.c src/cache_expire.c $(STORE_SOURCES) iniparser3.0b/libiniparser.la 
gen_tile_test_CFLAGS = -DMAIN_ALREADY_DEFINED $(PTHREAD_CFLAGS)
gen_tile_test_LDADD = $(FT2_LIBS) $(MAPNIK_LDFLAGS) $(BOOST_LDFLAGS) $(ICU_LDFLAGS) $(STORE_LDFLAGS) -Liniparser3.0b/.libs -liniparser

CLEANFILES=*.slo mod_tile.la stderr.out src/*.slo src/*.lo src/.libs/* src/*.la


test: gen_tile_test
	./gen_tile_test

all-local:
	$(APXS) -c $(DEF_LDLIBS) $(AM_CFLAGS) -I@srcdir@/includes $(AM_LDFLAGS) $(STORE_LDFLAGS) @srcdir@/src/mod_tile.c  @srcdir@/src/sys_utils.c @srcdir@/src/store.c @srcdir@/src/store_file.c @srcdir@/src/store_file_utils.c @srcdir@/src/store_memcached.c @srcdir@/src/store_rados.c @srcdir@/src/store_ro_http_proxy.c @srcdir@/src/store_ro_composite.c @srcdir@/src/store_null.c

install-mod_tile: 
	mkdir -p $(DESTDIR)`$(APXS) -q LIBEXECDIR`
	$(APXS) -S LIBEXECDIR=$(DESTDIR)`$(APXS) -q LIBEXECDIR` -c -i $(DEF_LDLIBS) $(AM_CFLAGS) -I@srcdir@/includes $(AM_LDFLAGS) $(STORE_LDFLAGS) @srcdir@/src/mod_tile.c @srcdir@/src/sys_utils.c @srcdir@/src/store.c @srcdir@/src/store_file.c @srcdir@/src/store_file_utils.c @srcdir@/src/store_memcached.c @srcdir@/src/store_rados.c @srcdir@/src/store_ro_http_proxy.c @srcdir@/src/store_ro_composite.c @srcdir@/src/store_null.c


