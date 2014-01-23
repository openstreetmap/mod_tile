COUCHBASE_SRCS=../src/store_couchbase.c ../src/store_memcached.c

meta2tile: meta2tile.c $(COUCHBASE_SRCS)
	gcc -O3 -std=gnu99 -I../includes -I/usr/include $(COUCHBASE_SRCS) meta2tile.c -o meta2tile -lm -lmemcached -lssl -Wall
