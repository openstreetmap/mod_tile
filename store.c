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


#include "store.h"
#include "store_file.h"
#include "store_memcached.h"
#include "store_rados.h"

//TODO: Make this function handle different logging backends, depending on if on compiles it from apache or something else
void log_message(int log_lvl, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int len;
    char *msg = malloc(1000*sizeof(char));

    if (msg) {
        len = vsnprintf(msg, 1000, format, ap);
        switch (log_lvl) {
        case STORE_LOGLVL_DEBUG:
            fprintf(stderr, "debug: %s\n", msg);
            break;
        case STORE_LOGLVL_INFO:
            fprintf(stderr, "info: %s\n", msg);
            break;
        case STORE_LOGLVL_WARNING:
            fprintf(stderr, "WARNING: %s\n", msg);
            break;
        case STORE_LOGLVL_ERR:
            fprintf(stderr, "ERROR: %s\n", msg);
            break;
        }
        free(msg);
    }
    va_end(ap);
}

struct storage_backend * init_storage_backend(const char * options) {
    struct stat st;
    struct storage_backend * store;

    //Determine the correct storage backend based on the options string
    if (strlen(options) == 0) {
        log_message(STORE_LOGLVL_ERR, "init_storage_backend: Options string was empty");
        return NULL;
    }
    if (options[0] == '/') {
        if (stat(options, &st) != 0) {
            log_message(STORE_LOGLVL_ERR, "init_storage_backend: Failed to stat %s with error: %s", options, strerror(errno));
            return NULL;
        }
        if (S_ISDIR(st.st_mode)) {
            log_message(STORE_LOGLVL_DEBUG, "init_storage_backend: initialising file storage backend at: %s for thread %lu", options, pthread_self());
             store = init_storage_file(options);
        } else {
            log_message(STORE_LOGLVL_ERR, "init_storage_backend: %s is not a directory", options, strerror(errno));
            return NULL;
        }
    }
    if (strstr(options,"rados://") == options) {
        log_message(STORE_LOGLVL_DEBUG, "init_storage_backend: initialising rados storage backend at: %s", options);
        store = init_storage_rados(options);
    }
    if (strstr(options,"memcached://") == options) {
        log_message(STORE_LOGLVL_DEBUG, "init_storage_backend: initialising memcached storage backend at: %s", options);
        store = init_storage_memcached(options);
    }


    return store;
}
