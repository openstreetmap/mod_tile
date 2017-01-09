# if you don't need multi-target output you can drop WITH_SHAPE and -lgdal /usr/lib/libgeos_c.so.1
# if you don't need mbtiles you can drop WITH_MBTILES and -lsqlite

meta2tile: meta2tile.c 
	gcc -DWITH_SHAPE -DWITH_MBTILES -O3 -std=gnu99 -I /usr/include/gdal -I../includes -I/usr/include  meta2tile.c -o meta2tile -lsqlite3 -lm -lgdal /usr/lib/libgeos_c.so.1
