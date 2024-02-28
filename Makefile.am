AUTOMAKE_OPTIONS = foreign

ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = $(PTHREAD_CFLAGS) $(GLIB_CFLAGS)

STORE_SOURCES = \
	src/g_logger.c \
	src/store.c \
	src/store_file.c \
	src/store_file_utils.c \
	src/store_memcached.c \
	src/store_null.c \
	src/store_rados.c \
	src/store_ro_composite.c \
	src/store_ro_http_proxy.c
STORE_LDFLAGS = $(LIBMEMCACHED_LDFLAGS) $(LIBRADOS_LDFLAGS) $(LIBCURL) $(GLIB_LIBS)

STORE_CPPFLAGS =

bin_PROGRAMS = \
	renderd \
	render_expired \
	render_list \
	render_old \
	render_speedtest
noinst_PROGRAMS = \
	gen_tile_test \
	renderd_config_test \
	renderd_test \
	render_expired_test \
	render_list_test \
	render_old_test \
	render_speedtest_test

man_MANS = \
	docs/man/renderd.1 \
	docs/man/renderd.conf.5 \
	docs/man/render_expired.1 \
	docs/man/render_list.1 \
	docs/man/render_old.1 \
	docs/man/render_speedtest.1

renderddir = $(sysconfdir)

renderd_SOURCES = \
	src/renderd.c \
	src/cache_expire.c \
	src/daemon_compat.c \
	src/gen_tile.cpp \
	src/metatile.cpp \
	src/parameterize_style.cpp \
	src/protocol_helper.c \
	src/renderd_config.c \
	src/request_queue.c \
	src/sys_utils.c \
	$(STORE_SOURCES)
renderd_CXXFLAGS = $(MAPNIK_CFLAGS)
renderd_DATA = etc/renderd/renderd.conf
renderd_LDADD = $(PTHREAD_CFLAGS) $(MAPNIK_LDFLAGS) $(STORE_LDFLAGS) $(INIPARSER_LDFLAGS)

render_speedtest_SOURCES = \
	src/render_speedtest.cpp \
	src/g_logger.c \
	src/protocol_helper.c \
	src/render_submit_queue.c \
	src/renderd_config.c \
	src/sys_utils.c
render_speedtest_LDADD = $(PTHREAD_CFLAGS) $(GLIB_LIBS) $(INIPARSER_LDFLAGS)

render_list_SOURCES = \
	src/render_list.c \
	src/protocol_helper.c \
	src/render_submit_queue.c \
	src/renderd_config.c \
	src/sys_utils.c \
	$(STORE_SOURCES)
render_list_LDADD = $(PTHREAD_CFLAGS) $(STORE_LDFLAGS) $(INIPARSER_LDFLAGS)

render_expired_SOURCES = \
	src/render_expired.c \
	src/protocol_helper.c \
	src/render_submit_queue.c \
	src/renderd_config.c \
	src/sys_utils.c \
	$(STORE_SOURCES)
render_expired_LDADD = $(PTHREAD_CFLAGS) $(STORE_LDFLAGS) $(INIPARSER_LDFLAGS)

render_old_SOURCES = \
	src/render_old.c \
	src/g_logger.c \
	src/protocol_helper.c \
	src/render_submit_queue.c \
	src/renderd_config.c \
	src/store_file_utils.c \
	src/sys_utils.c
render_old_LDADD = $(PTHREAD_CFLAGS) $(GLIB_LIBS) $(INIPARSER_LDFLAGS)

#convert_meta_SOURCES = src/dir_utils.c src/store.c src/convert_meta.c

noinst_LIBRARIES = catch_main.o catch_test_common.o
catch_main_o_SOURCES = tests/catch_main.cpp
catch_test_common_o_SOURCES = tests/catch_test_common.cpp

gen_tile_test_SOURCES = \
	tests/gen_tile_test.cpp \
	$(renderd_SOURCES)
gen_tile_test_CFLAGS = -DMAIN_ALREADY_DEFINED
gen_tile_test_CXXFLAGS = $(renderd_CXXFLAGS)
gen_tile_test_LDADD = $(renderd_LDADD) catch_test_common.o

renderd_config_test_SOURCES = \
	tests/renderd_config_test.cpp
renderd_config_test_LDADD = $(GLIB_LIBS) catch_main.o catch_test_common.o

renderd_test_SOURCES = \
	tests/renderd_test.cpp
renderd_test_LDADD = $(GLIB_LIBS) catch_main.o catch_test_common.o

render_expired_test_SOURCES = \
	tests/render_expired_test.cpp
render_expired_test_LDADD = $(GLIB_LIBS) catch_main.o catch_test_common.o

render_list_test_SOURCES = \
	tests/render_list_test.cpp
render_list_test_LDADD = $(GLIB_LIBS) catch_main.o catch_test_common.o

render_old_test_SOURCES = \
	tests/render_old_test.cpp
render_old_test_LDADD = $(GLIB_LIBS) catch_main.o catch_test_common.o

render_speedtest_test_SOURCES = \
	tests/render_speedtest_test.cpp
render_speedtest_test_LDADD = $(GLIB_LIBS) catch_main.o catch_test_common.o

CLEANFILES=*.slo mod_tile.la stderr.out src/*.slo src/*.lo src/.libs/* src/*.la

COMMA=,

test: gen_tile_test renderd_config_test renderd_test render_expired_test render_list_test render_old_test render_speedtest_test
	./gen_tile_test
	./renderd_config_test
	./renderd_test
	./render_expired_test
	./render_list_test
	./render_old_test
	./render_speedtest_test

all-local:
	$(APXS) -c $(DEF_LDLIBS) $(AM_CFLAGS) \
		$(subst -pthread,-Wc$(COMMA)-pthread,$(GLIB_CFLAGS)) \
		-I@srcdir@/includes $(AM_LDFLAGS) $(STORE_LDFLAGS) $(INIPARSER_LDFLAGS) \
			@srcdir@/src/mod_tile.c  \
			@srcdir@/src/g_logger.c \
			@srcdir@/src/renderd_config.c \
			@srcdir@/src/store.c \
			@srcdir@/src/store_file.c \
			@srcdir@/src/store_file_utils.c \
			@srcdir@/src/store_memcached.c \
			@srcdir@/src/store_null.c \
			@srcdir@/src/store_rados.c \
			@srcdir@/src/store_ro_composite.c \
			@srcdir@/src/store_ro_http_proxy.c \
			@srcdir@/src/sys_utils.c

install-mod_tile:
	mkdir -p $(DESTDIR)`$(APXS) -q LIBEXECDIR`
	$(APXS) -S LIBEXECDIR=$(DESTDIR)`$(APXS) \
		-q LIBEXECDIR` -c -i $(DEF_LDLIBS) $(AM_CFLAGS) \
		$(subst -pthread,-Wc$(COMMA)-pthread,$(GLIB_CFLAGS)) \
		-I@srcdir@/includes $(AM_LDFLAGS) $(STORE_LDFLAGS) $(INIPARSER_LDFLAGS) \
			@srcdir@/src/mod_tile.c \
			@srcdir@/src/g_logger.c \
			@srcdir@/src/renderd_config.c \
			@srcdir@/src/store.c \
			@srcdir@/src/store_file.c \
			@srcdir@/src/store_file_utils.c \
			@srcdir@/src/store_memcached.c \
			@srcdir@/src/store_null.c \
			@srcdir@/src/store_rados.c \
			@srcdir@/src/store_ro_composite.c \
			@srcdir@/src/store_ro_http_proxy.c \
			@srcdir@/src/sys_utils.c
