/*
 *
 * Copyright © 2013 mod_tile contributors
 * Copyright © 2013 Kai Krueger
 * Copyright © 2013 Dane Springmeyer
 *
 *This file is part of renderd, a project to render OpenStreetMap tiles
 *with Mapnik.
 *
 * renderd is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 2 of the License, or (at your
 * option) any later version.
 *
 * mod_tile is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mod_tile.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>

// https://github.com/philsquared/Catch/wiki/Supplying-your-own-main()
#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include "metatile.h"
#include "gen_tile.h"
#include "render_config.h"
#include "request_queue.h"
#include "store.h"
#include <syslog.h>
#include <sstream>
#include "string.h"
#include <string>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdlib.h>

#include <mapnik/version.hpp>
#if MAPNIK_VERSION < 200000
#include <mapnik/envelope.hpp>
#define image_32 Image32
#define image_data_32 ImageData32
#define box2d Envelope
#define zoom_to_box zoomToBox
#else
#include <mapnik/box2d.hpp>
#endif


#define NO_QUEUE_REQUESTS 9
#define NO_TEST_REPEATS 100
#define NO_THREADS 100

extern struct projectionconfig * get_projection(const char * srs);
extern mapnik::box2d<double> tile2prjbounds(struct projectionconfig * prj, int x, int y, int z);

std::string get_current_stderr() {
    FILE * input = fopen("stderr.out", "r+");
    std::string log_lines;
    unsigned sz = 1024;
    char buffer[sz];
    while (fgets(buffer, 512, input))
    {
        log_lines += buffer;
    }
    // truncate the file now so future reads
    // only get the new stuff
    FILE * input2 = fopen("stderr.out", "w");
    fclose(input2);
    fclose(input);
    return log_lines;
}

struct item * init_render_request(enum protoCmd type) {
    static int counter;
    struct item * item = (struct item *)malloc(sizeof(struct item));
    bzero(item, sizeof(struct item));
    item->req.ver = PROTO_VER;
    strcpy(item->req.xmlname,"default");
    item->req.cmd = type;
    item->mx = counter++;
    return item;
}

void *addition_thread(void * arg) {
    struct request_queue * queue = (struct request_queue *)arg;
    struct item * item;
    enum protoCmd res;
    struct timespec time;
    unsigned int seed = syscall(SYS_gettid);
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time);
    seed *= (unsigned int)time.tv_nsec;
    pthread_t tid = pthread_self();

    for (int i = 0; i < NO_QUEUE_REQUESTS; i++) {
        item = init_render_request(cmdDirty);
        item->my = tid;
        item->mx = rand_r(&seed);
        res = request_queue_add_request(queue, item);
    }
    return NULL;
}

void * fetch_thread(void * arg) {
    struct request_queue * queue = (struct request_queue *)arg;
    struct item * item;
    enum protoCmd res;

    for (int i = 0; i < NO_QUEUE_REQUESTS; i++) {
        item = request_queue_fetch_request(queue);
    }
    return NULL;
}

TEST_CASE( "renderd/queueing", "request queueing") {
    SECTION("renderd/queueing/initialisation", "test the initialisation of the request queue") {
        request_queue * queue = request_queue_init();
        REQUIRE( queue != NULL );
        request_queue_close(queue);
    }

    SECTION("renderd/queueing/simple request add", "test the addition of a single request") {
        request_queue * queue = request_queue_init();
        struct item * item = init_render_request(cmdRender);

        enum protoCmd res = request_queue_add_request(queue, item);
        REQUIRE ( res == cmdIgnore );

        request_queue_close(queue);
    }

    SECTION("renderd/queueing/simple request add priority", "test the addition of requests with different priorities") {
        struct item * item;
        enum protoCmd res;
        request_queue * queue = request_queue_init();

        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 0 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRender) == 0 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderLow) == 0 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 0 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 0 );
        item = init_render_request(cmdRender);
        res = request_queue_add_request(queue, item);
        REQUIRE ( res == cmdIgnore );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRender) == 1 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 0 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderLow) == 0 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 0 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 0 );
        item = init_render_request(cmdRenderPrio);
        res = request_queue_add_request(queue, item);
        REQUIRE ( res == cmdIgnore );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 1 );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRender) == 1 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderLow) == 0 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 0 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 0 );
        item = init_render_request(cmdRenderLow);
        res = request_queue_add_request(queue, item);
        REQUIRE ( res == cmdIgnore );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderLow) == 1 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 1 );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRender) == 1 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 0 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 0 );

        item = init_render_request(cmdRenderBulk);
        res = request_queue_add_request(queue, item);
        REQUIRE ( res == cmdIgnore );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 1 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 1 );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRender) == 1 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderLow) == 1 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 0 );

        item = init_render_request(cmdDirty);
        res = request_queue_add_request(queue, item);
        REQUIRE ( res == cmdNotDone );
        REQUIRE(request_queue_no_requests_queued(queue, cmdDirty) == 1 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderBulk) == 1 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderPrio) == 1 );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRender) == 1 );
        REQUIRE(request_queue_no_requests_queued(queue, cmdRenderLow) == 1 );


        request_queue_close(queue);
    }

    SECTION("renderd/queueing/simple request fetch", "test the fetching of a single request") {
        request_queue * queue = request_queue_init();

        struct item * item = init_render_request(cmdRender);
        request_queue_add_request(queue, item);
        struct item *item2 = request_queue_fetch_request(queue);
        REQUIRE( item == item2 );
        request_queue_remove_request(queue,item2, 0);
        free(item2);

        item = init_render_request(cmdRenderPrio);
        request_queue_add_request(queue, item);
        item2 = request_queue_fetch_request(queue);
        REQUIRE( item == item2 );
        request_queue_remove_request(queue,item2, 0);
        free(item2);

        item = init_render_request(cmdRenderLow);
        request_queue_add_request(queue, item);
        item2 = request_queue_fetch_request(queue);
        REQUIRE( item == item2 );
        request_queue_remove_request(queue,item2, 0);
        free(item2);

        item = init_render_request(cmdRenderBulk);
        request_queue_add_request(queue, item);
        item2 = request_queue_fetch_request(queue);
        REQUIRE( item == item2 );
        request_queue_remove_request(queue,item2, 0);
        free(item2);

        item = init_render_request(cmdDirty);
        request_queue_add_request(queue, item);
        item2 = request_queue_fetch_request(queue);
        REQUIRE( item == item2 );
        request_queue_remove_request(queue,item2, 0);
        free(item2);

        request_queue_close(queue);
    }

    SECTION("renderd/queueing/simple request fetch priority", "test the fetching of requests with different priorities and their ordering") {
        struct item *item2;

        request_queue * queue = request_queue_init();

        struct item * itemR = init_render_request(cmdRender);
        request_queue_add_request(queue, itemR);
        struct item * itemB = init_render_request(cmdRenderBulk);
        request_queue_add_request(queue, itemB);
        struct item * itemD = init_render_request(cmdDirty);
        request_queue_add_request(queue, itemD);
        struct item * itemRP = init_render_request(cmdRenderPrio);
        request_queue_add_request(queue, itemRP);
        struct item * itemL = init_render_request(cmdRenderLow);
        request_queue_add_request(queue, itemL);

        //We should be retrieving items in the order RenderPrio, Render, Dirty, Bulk
        item2 = request_queue_fetch_request(queue);
        INFO("itemRP: " << itemRP);
        INFO("itemR: " << itemR);
        INFO("itemL: " << itemL);
        INFO("itemD: " << itemD);
        INFO("itemB: " << itemB);
        REQUIRE( itemRP == item2 );
        request_queue_remove_request(queue,item2, 0);
        item2 = request_queue_fetch_request(queue);
        REQUIRE( itemR == item2 );
        request_queue_remove_request(queue,item2, 0);
        item2 = request_queue_fetch_request(queue);
        REQUIRE( itemL == item2 );
        request_queue_remove_request(queue,item2, 0);
        item2 = request_queue_fetch_request(queue);
        REQUIRE( itemD == item2 );
        request_queue_remove_request(queue,item2, 0);
        item2 = request_queue_fetch_request(queue);
        REQUIRE( itemB == item2 );

        free(itemR);
        free(itemB);
        free(itemD);
        free(itemRP);
        free(itemL);

        request_queue_close(queue);
        }

    SECTION("renderd/queueing/pending requests", "test if de-duplication of requests work") {
        enum protoCmd res;
        struct item * item;
        request_queue * queue = request_queue_init();

        //Submitting initial request
        item = init_render_request(cmdRender);
        item->mx = 0;
        res = request_queue_add_request(queue, item);
        REQUIRE( res == cmdIgnore );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRender) == 1 );

        //Submit duplicate request, check that queue length hasn't increased
        item = init_render_request(cmdRender);
        item->mx = 0;
        res = request_queue_add_request(queue, item);
        REQUIRE( res == cmdIgnore );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRender) == 1 );

        //Submit second request
        item = init_render_request(cmdRender);
        item->mx = 1;
        res = request_queue_add_request(queue, item);
        REQUIRE( res == cmdIgnore );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRender) == 2 );

        //Submit first request to render prio
        item = init_render_request(cmdRenderPrio);
        item->mx = 2;
        res = request_queue_add_request(queue, item);
        REQUIRE( res == cmdIgnore );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRenderPrio) == 1 );

        //Submit duplicate to render prio
        item = init_render_request(cmdRenderPrio);
        item->mx = 2;
        res = request_queue_add_request(queue, item);
        REQUIRE( res == cmdIgnore );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRenderPrio) == 1 );

        //Submit duplicate to dirty, check that de-duplication works across queues
        item = init_render_request(cmdDirty);
        item->mx = 2;
        res = request_queue_add_request(queue, item);
        REQUIRE( res == cmdIgnore );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRenderPrio) == 1 );
        REQUIRE( request_queue_no_requests_queued(queue, cmdDirty) == 0 );

        //Submit duplicate to request low, check that de-duplication works across queues
        item = init_render_request(cmdRenderLow);
        item->mx = 2;
        res = request_queue_add_request(queue, item);
        REQUIRE( res == cmdIgnore );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRenderPrio) == 1 );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRenderLow) == 0 );

        //Submit duplicate to request low, check that de-duplication works across queues
        item = init_render_request(cmdRender);
        item->mx = 2;
        res = request_queue_add_request(queue, item);
        REQUIRE( res == cmdIgnore );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRenderPrio) == 1 );
        //There were 2 render request submitted earlier in the test, so a
        //number of 2 is the same as before.
        REQUIRE( request_queue_no_requests_queued(queue, cmdRender) == 2 );

        //Submit duplicate to bulk, check that de-duplication works across queues
        item = init_render_request(cmdRenderBulk);
        item->mx = 2;
        res = request_queue_add_request(queue, item);
        REQUIRE( res == cmdIgnore );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRenderPrio) == 1 );
        REQUIRE( request_queue_no_requests_queued(queue, cmdRenderBulk) == 0 );



        request_queue_close(queue);
    }

    SECTION("renderd/queueing/overflow requests", "test if requests correctly overflow from one request priority to the next") {
        enum protoCmd res;
        struct item * item;
        request_queue * queue = request_queue_init();
        for (int i = 1; i < (2*REQ_LIMIT + DIRTY_LIMIT + 2); i++) {
                item = init_render_request(cmdRenderPrio);
                res = request_queue_add_request(queue, item);
                INFO("i: " << i);
                INFO("NoPrio: " << request_queue_no_requests_queued(queue, cmdRenderPrio));
                INFO("NoRend: " << request_queue_no_requests_queued(queue, cmdRender));
                INFO("NoDirt: " << request_queue_no_requests_queued(queue, cmdDirty));
                INFO("NoBulk: " << request_queue_no_requests_queued(queue, cmdRenderBulk));
                if (i <= REQ_LIMIT) {
                    REQUIRE( res == cmdIgnore );
                    REQUIRE( request_queue_no_requests_queued(queue, cmdRenderPrio) == i );
                } else if (i <= (REQ_LIMIT + DIRTY_LIMIT)) {
                    //Requests should overflow into the dirty queue
                    REQUIRE( res == cmdNotDone );
                    REQUIRE( request_queue_no_requests_queued(queue, cmdRenderPrio) == REQ_LIMIT );
                    REQUIRE( request_queue_no_requests_queued(queue, cmdDirty) == (i - REQ_LIMIT) );
                } else {
                    //Requests should be dropped alltogether
                    REQUIRE( res == cmdNotDone );
                    REQUIRE( request_queue_no_requests_queued(queue, cmdRenderPrio) == REQ_LIMIT );
                    REQUIRE( request_queue_no_requests_queued(queue, cmdDirty) == DIRTY_LIMIT);
                }
        }
        request_queue_close(queue);
    }

    SECTION("renderd/queueing/multithreading request addition", "test if there are any issues with multithreading") {
        pthread_t * addition_threads;
        request_queue * queue;

        REQUIRE ( (NO_THREADS * NO_QUEUE_REQUESTS) < DIRTY_LIMIT );

        for (int j = 0; j < NO_TEST_REPEATS; j++) { //As we are looking for race conditions, repeat this test many times
            addition_threads = (pthread_t *)calloc(NO_THREADS,sizeof(pthread_t));
            queue = request_queue_init();
            void *status;

            for (int i = 0; i < NO_THREADS; i++) {
                if (pthread_create(&addition_threads[i], NULL, addition_thread,
                        (void *) queue)) {
                    INFO("Failed to create thread");
                    REQUIRE( 1 == 0 );
                }
            }

            for (int i = 0; i < NO_THREADS; i++) {
                pthread_join(addition_threads[i], &status);
            }

            INFO("Itteration " << j);
            REQUIRE( request_queue_no_requests_queued(queue, cmdDirty) == (NO_THREADS * NO_QUEUE_REQUESTS) );

            request_queue_close(queue);
            free(addition_threads);
        }
    }

    SECTION("renderd/queueing/multithreading request fetch", "test if there are any issues with multithreading") {
            pthread_t * fetch_threads;
            struct request_queue * queue;
            struct item * item;

            for (int j = 0; j < NO_TEST_REPEATS; j++) { //As we are looking for race conditions, repeat this test many times
                fetch_threads = (pthread_t *)calloc(NO_THREADS,sizeof(pthread_t));
                queue = request_queue_init();
                void *status;

                for (int i = 0; i < (NO_THREADS * NO_QUEUE_REQUESTS); i++) {
                    item = init_render_request(cmdDirty);
                    request_queue_add_request(queue, item);
                }

                REQUIRE( request_queue_no_requests_queued(queue, cmdDirty) == (NO_THREADS*NO_QUEUE_REQUESTS) );


                for (int i = 0; i  < NO_THREADS; i++) {
                    if (pthread_create(&fetch_threads[i], NULL, fetch_thread,
                            (void *) queue)) {
                        INFO("Failed to create thread");
                        REQUIRE( 1 == 0 );
                    }
                }

                for (int i = 0; i < NO_THREADS; i++) {
                    pthread_join(fetch_threads[i], &status);
                }

                INFO("Itteration " << j);
                REQUIRE( request_queue_no_requests_queued(queue, cmdDirty) == 0 );

                request_queue_close(queue);
                free(fetch_threads);
            }
        }

    SECTION("renderd/queueing/multithreading request fetch on empty queue", "test if there are any issues with multithreading") {
        pthread_t * fetch_threads;
        pthread_t * addition_threads;
        struct request_queue * queue;
        struct item * item;

        for (int j = 0; j < NO_TEST_REPEATS; j++) { //As we are looking for race conditions, repeat this test many times
            fetch_threads = (pthread_t *)calloc(NO_THREADS,sizeof(pthread_t));
            addition_threads = (pthread_t *)calloc(NO_THREADS,sizeof(pthread_t));
            queue = request_queue_init();
            void *status;

            for (int i = 0; i  < NO_THREADS; i++) {
                if (pthread_create(&fetch_threads[i], NULL, fetch_thread,
                        (void *) queue)) {
                    INFO("Failed to create thread");
                    REQUIRE( 1 == 0 );
                }
            }

            for (int i = 0; i  < NO_THREADS; i++) {
                if (pthread_create(&addition_threads[i], NULL, addition_thread,
                        (void *) queue)) {
                    INFO("Failed to create thread");
                    REQUIRE( 1 == 0 );
                }
            }

            for (int i = 0; i < NO_THREADS; i++) {
                pthread_join(fetch_threads[i], &status);
                pthread_join(addition_threads[i], &status);
            }

            INFO("Itteration " << j);
            REQUIRE( request_queue_no_requests_queued(queue, cmdDirty) == 0 );

            request_queue_close(queue);
            free(fetch_threads);
            free(addition_threads);
        }
    }


    SECTION("renderd/queueing/clear fd", "Test if the clearing of fd work for queues") {
        struct request_queue * queue = request_queue_init();
        struct item * item;

        item = init_render_request(cmdRender);
        item->fd  = 1;
        request_queue_add_request(queue, item);
        item = init_render_request(cmdRender);
        item->fd  = 2;
        request_queue_add_request(queue, item);
        request_queue_clear_requests_by_fd(queue, 2);
        item = init_render_request(cmdRender);
        item->fd  = 3;
        request_queue_add_request(queue, item);
        item = init_render_request(cmdRender);
        item->fd  = 4;
        request_queue_add_request(queue, item);
        item = init_render_request(cmdRender);
        item->fd  = 5;
        request_queue_add_request(queue, item);
        item = init_render_request(cmdRender);
        item->fd  = 6;
        request_queue_add_request(queue, item);
        request_queue_clear_requests_by_fd(queue, 4);

        item = request_queue_fetch_request(queue);
        REQUIRE (item->fd == 1);
        request_queue_remove_request(queue,item, 0);
        item = request_queue_fetch_request(queue);
        REQUIRE (item->fd == FD_INVALID);
        request_queue_remove_request(queue,item, 0);
        free(item);
        item = request_queue_fetch_request(queue);
        REQUIRE (item->fd == 3);
        request_queue_remove_request(queue,item, 0);
        free(item);
        item = request_queue_fetch_request(queue);
        REQUIRE (item->fd == FD_INVALID);
        request_queue_remove_request(queue,item, 0);
        free(item);
        item = request_queue_fetch_request(queue);
        REQUIRE (item->fd == 5);
        request_queue_remove_request(queue,item, 0);
        free(item);
        item = request_queue_fetch_request(queue);
        REQUIRE (item->fd == 6);
        request_queue_remove_request(queue,item, 0);
        free(item);

        request_queue_close(queue);
    }
}

TEST_CASE( "renderd", "tile generation" ) {

      SECTION("render_init 1", "should throw nice error if paths are invalid") {
          render_init("doesnotexist","doesnotexist",1);
          std::string log_lines = get_current_stderr();
          int found = log_lines.find("Unable to open font directory: doesnotexist");
          //std::cout << "a: " << log_lines << "\n";
          REQUIRE( found > -1 );
      }

      // we run this test twice to ensure that our stderr reading is working correctlyu
      SECTION("render_init 2", "should throw nice error if paths are invalid") {
          render_init("doesnotexist","doesnotexist",1);
          std::string log_lines = get_current_stderr();
          int found = log_lines.find("Unable to open font directory: doesnotexist");
          //std::cout << "b: " << log_lines << "\n";
          REQUIRE( found > -1 );
      }

      SECTION("renderd startup --help", "should start and show help message") {
          int ret = system("./renderd -h");
          ret = WEXITSTATUS(ret);
          //CAPTURE( ret );
          REQUIRE( ret == 0 );
      }

      SECTION("renderd startup unrecognized option", "should return 1") {
          int ret = system("./renderd --doesnotexit");
          ret = WEXITSTATUS(ret);
          //CAPTURE( ret );
          REQUIRE( ret == 1 );
      }

      SECTION("renderd startup invalid option", "should return 1") {
          int ret = system("./renderd -doesnotexit");
          ret = WEXITSTATUS(ret);
          //CAPTURE( ret );
          REQUIRE( ret == 1 );
      }
}

TEST_CASE( "storage-backend", "Tile storage backend" ) {

    /* Setting up directory where to test the tiles in */
    char * tmp;
    char * tile_dir;

    tmp = getenv("TMPDIR");
    if (tmp == NULL) {
        tmp = P_tmpdir;
    }
    tile_dir = (char *) malloc(sizeof(char) * (strlen(tmp) + 15));
    sprintf(tile_dir,"%s/mod_tile_test",tmp);
    mkdir(tile_dir, 0777);

    SECTION("storage/initialise", "should return 1") {
        struct storage_backend * store = NULL;

        store = init_storage_backend(tile_dir);
        REQUIRE( store != NULL );
        store->close_storage(store);
        
    }

    SECTION("storage/stat/non existent", "should return 0 size") {
        struct storage_backend * store = NULL;
        struct stat_info sinfo;

        store = init_storage_backend(tile_dir);
        REQUIRE( store != NULL );
        
        sinfo = store->tile_stat(store, "default", "", 0, 0, 0);
        REQUIRE( sinfo.size < 0 );
        store->close_storage(store);
        
    }

    SECTION("storage/read/non existent", "should return 0 size") {
        struct storage_backend * store = NULL;
        int size;
        char * buf = (char *)malloc(10000);
        int compressed;
        char * err_msg = (char *)malloc(10000);

        store = init_storage_backend(tile_dir);
        REQUIRE( store != NULL );
        
        size = store->tile_read(store, "default", "", 0, 0, 0, buf, 10000, &compressed, err_msg);
        REQUIRE( size < 0 );
        
        store->close_storage(store);
        free(buf);
        free(err_msg);
        
    }

    SECTION("storage/write/full metatile", "should complete") {
        struct storage_backend * store = NULL;

        store = init_storage_backend(tile_dir);
        REQUIRE( store != NULL );
        
        metaTile tiles("default", "", 1024, 1024, 10);
        for (int yy = 0; yy < METATILE; yy++) {
            for (int xx = 0; xx < METATILE; xx++) {
                std::string tile_data = "DEADBEAF";
                tiles.set(xx, yy, tile_data);
            }
        }
        tiles.save(store);

        store->close_storage(store);
    }

    SECTION("storage/stat/full metatile", "should complete") {
        struct storage_backend * store = NULL;
        struct stat_info sinfo;
        
        time_t before_write, after_write;

        store = init_storage_backend(tile_dir);
        REQUIRE( store != NULL );
        
        metaTile tiles("default", "", 1024 + METATILE, 1024, 10);
        time(&before_write);
        for (int yy = 0; yy < METATILE; yy++) {
            for (int xx = 0; xx < METATILE; xx++) {
                std::string tile_data = "DEADBEAF";
                tiles.set(xx, yy, tile_data);
            }
        }
        tiles.save(store);
        time(&after_write);

        for (int yy = 0; yy < METATILE; yy++) {
            for (int xx = 0; xx < METATILE; xx++) {
                sinfo = store->tile_stat(store, "default", "", 1024 + METATILE + yy, 1024 + xx, 10);
                REQUIRE( sinfo.size > 0 );
                REQUIRE( sinfo.expired == 0 );
                REQUIRE( sinfo.atime > 0 );
                REQUIRE( sinfo.mtime >= before_write);
                REQUIRE( sinfo.mtime <= before_write);
            }
        }

        store->close_storage(store);
    }

    SECTION("storage/read/full metatile", "should complete") {
        struct storage_backend * store = NULL;
        char * buf;
        char * buf_tmp;
        char msg[4096];
        int compressed;
        int tile_size;

        buf = (char *)malloc(8196);
        buf_tmp = (char *)malloc(8196);

        time_t before_write, after_write;

        store = init_storage_backend(tile_dir);
        REQUIRE( store != NULL );
        
        metaTile tiles("default", "", 1024 + METATILE, 1024, 10);
        time(&before_write);
        for (int yy = 0; yy < METATILE; yy++) {
            for (int xx = 0; xx < METATILE; xx++) {
                sprintf(buf, "DEADBEAF %i %i", xx, yy);
                std::string tile_data(buf);
                tiles.set(xx, yy, tile_data);
            }
        }
        tiles.save(store);
        time(&after_write);

        for (int yy = 0; yy < METATILE; yy++) {
            for (int xx = 0; xx < METATILE; xx++) {
                tile_size = store->tile_read(store, "default", "", 1024 + METATILE + xx, 1024 + yy, 10, buf, 8195, &compressed, msg);
                REQUIRE ( tile_size == 12 );
                sprintf(buf_tmp, "DEADBEAF %i %i", xx, yy);
                REQUIRE ( memcmp(buf_tmp, buf, 11) == 0 );
            }
        }

        free(buf);
        free(buf_tmp);
        store->close_storage(store);


    }

    SECTION("storage/read/partial metatile", "should return correct data") {
        struct storage_backend * store = NULL;
        char * buf;
        char * buf_tmp;
        char msg[4096];
        int compressed;
        int tile_size;

        buf = (char *)malloc(8196);
        buf_tmp = (char *)malloc(8196);

        time_t before_write, after_write;

        store = init_storage_backend(tile_dir);
        REQUIRE( store != NULL );
        
        metaTile tiles("default", "",  1024 + 2*METATILE, 1024, 10);
        time(&before_write);
        for (int yy = 0; yy < METATILE; yy++) {
            for (int xx = 0; xx < (METATILE >> 1); xx++) {
                sprintf(buf, "DEADBEAF %i %i", xx, yy);
                std::string tile_data(buf);
                tiles.set(xx, yy, tile_data);
            }
        }
        tiles.save(store);
        time(&after_write);

        for (int yy = 0; yy < METATILE; yy++) {
            for (int xx = 0; xx < METATILE; xx++) {
                tile_size = store->tile_read(store, "default", "", 1024 + 2*METATILE + xx, 1024 + yy, 10, buf, 8195, &compressed, msg);
                if (xx >= (METATILE >> 1)) {
                    REQUIRE ( tile_size == 0 );
                } else {
                    REQUIRE ( tile_size == 12 );
                    sprintf(buf_tmp, "DEADBEAF %i %i", xx, yy);
                    REQUIRE ( memcmp(buf_tmp, buf, 11) == 0 );
                }
            }
        }

        free(buf);
        free(buf_tmp);
        store->close_storage(store);
    }

     SECTION("storage/expire/delete metatile", "should delete tile from disk") {
        struct storage_backend * store = NULL;
        struct stat_info sinfo;
        char * buf;
        char * buf_tmp;
        char msg[4096];
        int compressed;
        int tile_size;

        buf = (char *)malloc(8196);
        buf_tmp = (char *)malloc(8196);

        store = init_storage_backend(tile_dir);
        REQUIRE( store != NULL );
        
        metaTile tiles("default", "", 1024 + 3*METATILE, 1024, 10);

        for (int yy = 0; yy < METATILE; yy++) {
            for (int xx = 0; xx < METATILE; xx++) {
                sprintf(buf, "DEADBEAF %i %i", xx, yy);
                std::string tile_data(buf);
                tiles.set(xx, yy, tile_data);
            }
        }
        tiles.save(store);

        sinfo = store->tile_stat(store, "default", "", 1024 + 3*METATILE, 1024, 10);

        REQUIRE ( sinfo.size > 0 );

        store->metatile_delete(store, "default", 1024 + 3*METATILE, 1024, 10);

        sinfo = store->tile_stat(store, "default", "", 1024 + 3*METATILE, 1024, 10);

        REQUIRE ( sinfo.size < 0 );

        free(buf);
        free(buf_tmp);
        store->close_storage(store);
    }

     SECTION("storage/expire/expiremetatile", "should expire the tile") {
        struct storage_backend * store = NULL;
        struct stat_info sinfo;
        char * buf;
        char * buf_tmp;
        char msg[4096];
        int compressed;
        int tile_size;

        buf = (char *)malloc(8196);
        buf_tmp = (char *)malloc(8196);

        store = init_storage_backend(tile_dir);
        REQUIRE( store != NULL );
        
        metaTile tiles("default", "", 1024 + 4*METATILE, 1024, 10);

        for (int yy = 0; yy < METATILE; yy++) {
            for (int xx = 0; xx < METATILE; xx++) {
                sprintf(buf, "DEADBEAF %i %i", xx, yy);
                std::string tile_data(buf);
                tiles.set(xx, yy, tile_data);
            }
        }
        tiles.save(store);

        sinfo = store->tile_stat(store, "default", "", 1024 + 4*METATILE, 1024, 10);

        REQUIRE ( sinfo.size > 0 );

        store->metatile_expire(store, "default", 1024 + 4*METATILE, 1024, 10);

        sinfo = store->tile_stat(store, "default", "", 1024 + 4*METATILE, 1024, 10);

        REQUIRE ( sinfo.size > 0 );
        REQUIRE ( sinfo.expired > 0 );

        free(buf);
        free(buf_tmp);
        store->close_storage(store);
    }

    rmdir(tile_dir);
    free(tile_dir);
}

TEST_CASE( "projections", "Test projections" ) {

    SECTION("projections/bounds/spherical", "should return 1") {
        mapnik::box2d<double> bbox;
        struct projectionconfig * prj = get_projection("+proj=merc +a=6378137 +b=6378137");
        bbox = tile2prjbounds(prj, 0,0,0);
        REQUIRE (bbox.minx() == -20037508.3428);
        REQUIRE (bbox.miny() == -20037508.3428);
        REQUIRE (bbox.maxx() ==  20037508.3428);
        REQUIRE (bbox.maxy() ==  20037508.3428);
        bbox = tile2prjbounds(prj, 0,0,10);
        //313086.06785625 = 2*20037508.3428 / (2^10 / 8)
        REQUIRE (bbox.minx() == -20037508.3428);
        REQUIRE (round(bbox.miny()) == 19724422.0);
        REQUIRE (round(bbox.maxx()) ==  -19724422.0);
        REQUIRE (bbox.maxy() ==  20037508.3428);
        bbox = tile2prjbounds(prj, ((1<<10) - METATILE),((1<<10) - METATILE),10);
        REQUIRE (round(bbox.minx()) == 19724422.0);
        REQUIRE (bbox.miny() == -20037508.3428);
        REQUIRE (bbox.maxx() ==  20037508.3428);
        REQUIRE (round(bbox.maxy()) == -19724422.0);
        free(prj);

        prj = get_projection("+proj=eqc +lat_ts=0 +lat_0=0 +lon_0=0 +x_0=0 +y_0=0 +ellps=WGS84 +datum=WGS84 +units=m +no_defs");
        bbox = tile2prjbounds(prj, 0,0,0);
        REQUIRE (bbox.minx() == -20037508.3428);
        REQUIRE (bbox.miny() == -10018754.1714);
        REQUIRE (bbox.maxx() ==  20037508.3428);
        REQUIRE (bbox.maxy() ==  10018754.1714);
        bbox = tile2prjbounds(prj, 0,0,10);
        //156543.033928125 = 2*20037508.3428 / (2^10 / 8 * 2) 
        REQUIRE (bbox.minx() == -20037508.3428);
        REQUIRE (round(bbox.miny()) == 9862211.0 );
        REQUIRE (round(bbox.maxx()) == -19880965.0);
        REQUIRE (bbox.maxy() ==  10018754.1714);
        bbox = tile2prjbounds(prj, (2*(1<<10) - METATILE),((1<<10) - METATILE),10);
        REQUIRE (round(bbox.minx()) == 19880965.0);
        REQUIRE (bbox.miny() == -10018754.1714);
        REQUIRE (bbox.maxx() ==  20037508.3428);
        REQUIRE (round(bbox.maxy()) == -9862211.0);
        free(prj);

        prj = get_projection("+proj=tmerc +lat_0=49 +lon_0=-2 +k=0.9996012717 +x_0=400000 +y_0=-100000 +ellps=airy +datum=OSGB36 +units=m +no_defs");
        bbox = tile2prjbounds(prj, 0,0,0);
        REQUIRE (bbox.minx() == 0.0);
        REQUIRE (bbox.miny() == 0.0);
        REQUIRE (bbox.maxx() ==   700000.0);
        REQUIRE (bbox.maxy() ==  1400000.0);
        bbox = tile2prjbounds(prj, 0,0,10);
        //5468.75 = 700000 / (2^10 / 8) 
        REQUIRE (bbox.minx() == 0);
        REQUIRE (round(bbox.miny()) == 1394531.0 );
        REQUIRE (round(bbox.maxx()) == 5469.0);
        REQUIRE (bbox.maxy() ==  1400000.0);
        bbox = tile2prjbounds(prj, ((1<<10) - METATILE),(2*(1<<10) - METATILE),10);
        REQUIRE (round(bbox.minx()) == 694531.0);
        REQUIRE (bbox.miny() == 0.0);
        REQUIRE (bbox.maxx() ==  700000.0);
        REQUIRE (round(bbox.maxy()) == 5469.0);
        free(prj);
    }
}

int main (int argc, char* const argv[])
{
  //std::ios_base::sync_with_stdio(false);
  // start by supressing stderr
  // this avoids noisy test output that is intentionally
  // testing for things that produce stderr and also
  // allows us to catch and read it in these tests to validate
  // the stderr contains the right messages
  // http://stackoverflow.com/questions/13533655/how-to-listen-to-stderr-in-c-c-for-sending-to-callback
  FILE * stream = freopen("stderr.out", "w", stderr);
  //setvbuf(stream, 0, _IOLBF, 0); // No Buffering
  openlog("renderd", LOG_PID | LOG_PERROR, LOG_DAEMON);
  int result = Catch::Main( argc, argv );
  return result;
}

