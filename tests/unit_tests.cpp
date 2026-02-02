#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

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
#define connect mocked_connect
#define exit mocked_exit
#define g_logger mocked_g_logger
#define getaddrinfo mocked_getaddrinfo
#define getloadavg mocked_getloadavg
#define malloc mocked_malloc
#define mkdir mocked_mkdir
#define open mocked_open
#define recv mocked_recv
#define socket mocked_socket
#define strndup mocked_strndup
#define strtok mocked_strtok
#define write mocked_write

extern "C" {
#include "cache_expire.c"
#include "protocol_helper.c"
#include "render_submit_queue.c"
#include "renderd_config.c"
#include "store_file.c"
#include "store_file_utils.c"
#include "sys_utils.c"

	struct storage_backend * init_storage_backend(const char * options)
	{
		struct storage_backend * store = init_storage_file(options);
		return store;
	}
}

#include "metatile.cpp"
#include "parameterize_style.cpp"

#undef asprintf
#undef connect
#undef exit
#undef g_logger
#undef getaddrinfo
#undef getloadavg
#undef malloc
#undef mkdir
#undef open
#undef recv
#undef socket
#undef strndup
#undef strtok
#undef write

#include "unit_test_cache_expire.cpp"
#include "unit_test_metatile.cpp"
#include "unit_test_parameterize_style.cpp"
#include "unit_test_protocol_helper.cpp"
#include "unit_test_render_submit_queue.cpp"
#include "unit_test_renderd_config.cpp"
#include "unit_test_sys_utils.cpp"
