/* wrapper for storage engines
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif


#include "store.h"
#include "store_file.h"
#include "store_memcached.h"
#include "store_rados.h"
#include "store_ro_http_proxy.h"
#include "store_ro_composite.h"
#include "store_null.h"
#include "g_logger.h"

extern int log_to_std_streams;

/**
 * In Apache 2.2, we call the init_storage_backend once per process. For mpm_worker and mpm_event multiple threads therefore use the same
 * storage context, and all storage backends need to be thread-safe in order not to cause issues with these mpm's
 *
 * In Apache 2.4, we call the init_storage_backend once per thread, and therefore each thread has its own storage context to work with.
 */
struct storage_backend * init_storage_backend(const char * options)
{
	struct stat st;
	struct storage_backend * store = NULL;

	//Determine the correct storage backend based on the options string
	if (strlen(options) == 0) {
		g_logger(G_LOG_LEVEL_ERROR, "init_storage_backend: Options string was empty");
		return NULL;
	}

	if (options[0] == '/') {
		if (stat(options, &st) != 0) {
			g_logger(G_LOG_LEVEL_ERROR, "init_storage_backend: Failed to stat %s with error: %s", options, strerror(errno));
			return NULL;
		}

		if (S_ISDIR(st.st_mode)) {
			g_logger(G_LOG_LEVEL_DEBUG, "init_storage_backend: initialising file storage backend at: %s", options);
			store = init_storage_file(options);
			return store;
		} else {
			g_logger(G_LOG_LEVEL_ERROR, "init_storage_backend: %s is not a directory", options, strerror(errno));
			return NULL;
		}
	}

	if (strstr(options, "rados://") == options) {
		g_logger(G_LOG_LEVEL_DEBUG, "init_storage_backend: initialising rados storage backend at: %s", options);
		store = init_storage_rados(options);
		return store;
	}

	if (strstr(options, "memcached://") == options) {
		g_logger(G_LOG_LEVEL_DEBUG, "init_storage_backend: initialising memcached storage backend at: %s", options);
		store = init_storage_memcached(options);
		return store;
	}

	if (strstr(options, "ro_http_proxy://") == options) {
		g_logger(G_LOG_LEVEL_DEBUG, "init_storage_backend: initialising ro_http_proxy storage backend at: %s", options);
		store = init_storage_ro_http_proxy(options);
		return store;
	}

	if (strstr(options, "composite:{") == options) {
		g_logger(G_LOG_LEVEL_DEBUG, "init_storage_backend: initialising ro_composite storage backend at: %s", options);
		store = init_storage_ro_composite(options);
		return store;
	}

	if (strstr(options, "null://") == options) {
		g_logger(G_LOG_LEVEL_DEBUG, "init_storage_backend: initialising null storage backend at: %s", options);
		store = init_storage_null();
		return store;
	}

	g_logger(G_LOG_LEVEL_ERROR, "init_storage_backend: No valid storage backend found for options: %s", options);

	return store;
}
