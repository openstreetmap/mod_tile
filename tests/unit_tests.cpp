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

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <utime.h>

#include "catch/catch.hpp"
#include "catch_test_common.hpp"

#include "config.h"
#include "metatile.h"

#ifndef MAPNIK_XML
#define MAPNIK_XML "./utils/example-map/mapnik.xml"
#endif

#ifndef MAPNIK_PLUGINS_DIR
#define MAPNIK_PLUGINS_DIR "/usr/local/lib64/mapnik/input"
#endif

#ifndef RENDERD_CONF
#define RENDERD_CONF "./etc/renderd/renderd.conf.examples"
#endif

#define asprintf mocked_asprintf
#define calloc mocked_calloc
#define connect mocked_connect
#define exit mocked_exit
#define g_logger mocked_g_logger
#define getaddrinfo mocked_getaddrinfo
#define getloadavg mocked_getloadavg
#define getnameinfo mocked_getnameinfo
#define getprotobyname mocked_getprotobyname
#define gmtime_r mocked_gmtime_r
#define malloc mocked_malloc
#define mkdir mocked_mkdir
#define open mocked_open
#define pthread_cond_init mocked_pthread_cond_init
#define pthread_cond_wait mocked_pthread_cond_wait
#define pthread_create mocked_pthread_create
#define pthread_mutex_init mocked_pthread_mutex_init
#define read mocked_read
#define recv mocked_recv
#define rename mocked_rename
#define sleep mocked_sleep
#define socket mocked_socket
#define strdup mocked_strdup
#define strndup mocked_strndup
#define strtok mocked_strtok
#define utime mocked_utime
#define write mocked_write

extern "C" {
#include "cache_expire.c"
#include "protocol_helper.c"
#include "render_submit_queue.c"
#include "renderd_config.c"
#include "request_queue.c"
#include "store.c"
#include "store_file.c"
#include "store_file_utils.c"
#include "store_memcached.c"
#include "store_null.c"
#include "store_rados.c"
#include "store_ro_composite.c"
#include "store_ro_http_proxy.c"
#include "sys_utils.c"
}

#include "metatile.cpp"
#include "parameterize_style.cpp"

#undef asprintf
#undef calloc
#undef connect
#undef exit
#undef g_logger
#undef getaddrinfo
#undef getloadavg
#undef getnameinfo
#undef getprotobyname
#undef gmtime_r
#undef malloc
#undef mkdir
#undef open
#undef pthread_cond_init
#undef pthread_cond_wait
#undef pthread_create
#undef pthread_mutex_init
#undef read
#undef recv
#undef rename
#undef sleep
#undef socket
#undef strdup
#undef strndup
#undef strtok
#undef utime
#undef write

#include "unit_test_cache_expire.cpp"
#include "unit_test_metatile.cpp"
#include "unit_test_parameterize_style.cpp"
#include "unit_test_protocol_helper.cpp"
#include "unit_test_render_submit_queue.cpp"
#include "unit_test_renderd_config.cpp"
#include "unit_test_request_queue.cpp"
#include "unit_test_store.cpp"
#include "unit_test_store_file.cpp"
#include "unit_test_store_file_utils.cpp"
#include "unit_test_store_memcached.cpp"
#include "unit_test_store_null.cpp"
#include "unit_test_store_rados.cpp"
#include "unit_test_store_ro_composite.cpp"
#include "unit_test_store_ro_http_proxy.cpp"
#include "unit_test_sys_utils.cpp"
