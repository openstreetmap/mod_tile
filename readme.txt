mod_tile
========
A program to efficiently render and serve map tiles for
www.openstreetmap.org map using Apache and Mapnik.

Requirements
============
OSM map data imported into PostgreSQL using osm2pgsql
Mapnik renderer along with the OSM.xml file and map
symbols, world_boundaries shapefiles. Apache with
development headers for APR module development.

Tile Rendering
==============

The rendering is implemented in a multithreaded process
called renderd which opens either a unix or tcp socket
and listens for requests to render tiles. It uses Mapnik
to render tiles using the rendering rules defined in
the configuration file /etc/renderd.conf. Its configuration
also allows to specify the number of rendering
threads.

The render daemon implements a queuing mechanism with multiple
priority levels to provide an as up-to-date viewing experience
given the available rendering resources. The highest priority
is for on the fly rendering of tiles not yet in the tile cache,
two priority levels for re-rendering out of date tiles on the fly
and two background batch rendering queues. The on the fly rendering
queues are limited to a short 32 metatile size to minimize latency.
The size of the main background queue is determined
at compile time, see: render_config.h

Tile serving
============

An Apache module called mod_tile enhances the regular
Apache file serving mechanisms to provide:

1) When tiles have expired it requests the rendering
daemon to render (or re-render) the tile.

2) Remapping of the file path to the hashed layout

3) Prioritizes rendering requests depending on the available
resources on the server and how out of date they are.

4) Use tile storage other than a plain posix file system.
e.g it can store tiles in a ceph object store, or proxy them
from another tile server.

5) Tile expiry. It estimates when the tile is next
likely to be rendered and adds the appropriate HTTP
cache expiry headers. This is a configurable heuristic.

To avoid problems with directories becoming too large and to avoid
too many tiny files.  Mod_tile / renderd store the rendered tiles
in "meta tiles" in a special hashed directory structure. These combine
8x8 actual tiles into a single metatile file.  This is a more efficient
use of disk space and inodes. For example, many sea tiles are 103 bytes
long. In the old scheme a meta tile of blank sea tiles would take
64 inodes of 4kB each, a total of 256kB. In this optimized scheme it
needs a single file of about 7kB. The metatiles are then stored
in the following directory structure:
/[base_dir]/[TileSetName]/[Z]/[xxxxyyyy]/[xxxxyyyy]/[xxxxyyyy]/[xxxxyyyy]/[xxxxyyyy].png
Where base_dir is a configurable base path for all tiles. TileSetName
is the name of the style sheet rendered. Z is the zoom level.
[xxxxyyyy] is an 8 bit number, with the first 4 bits taken from the x
coordinate and the second 4 bits taken from the y coordinate. This
attempts to cluster 16x16 square of tiles together into a single sub
directory for more efficient access patterns.

Apache serves the files as if they were present
under "/[TileSetName]/Z/X/Y.png" with the path being
converted automatically.

Compiling
=========

mod_tile and renderd utilize a number of third party, some of which it
depends on and some provide optional features and are compiled in
if the respective libraries are installed. Once the dependencies are installed
you can compile and install mod_tile / renderd the usual way:

./autogen.sh
./configure
make
sudo make install
sudo make install-mod_tile


Setup
=====
Create a new apache config file to load the module,
e.g.

/etc/httpd/conf.d/mod_tile.conf

See the sample mod_tile.conf for details

Edit /etc/renderd.conf to indicate the location of your
mapnik style sheet and the uri you wish to use to access
it.  You may configure up to 10 (by default) mapnik
style sheets - simply give each section a unique name and
enter the uri and style sheet path.

Make sure the /var/lib/mod_tile directory is writable by 
the user running the renderd process and create a file an
empty file planet-import-complete in this folder.

Run the rendering daemon 'renderd'

Restart Aapche

Note: SELinux will prevent the mod_tile code from opening
the unix-socket to the render daemon so must be disabled.

Try loading a tile in your browser, e.g.
http://localhost/osm_tiles/0/0/0.png

The render daemon should have produce a message like:

Got incoming connection, fd 7, number 1
Render fd(7) xml(Default), z(0), x(0), y(0)

The disk should start thrashing as Mapnik tries to pull
in data for the first time. After a few seconds you'll
probably see a 404 error. Wait for the disk activity to
cease and then reload the tile. With a bit of luck you
should see a tile of the world in your browser window.

If this fails to happen check the http error log.  You can 
increase the level of debugging using the LogLevel apache
directive.  If no log messages are shown check that you
are accessing the correct virtual host - the new version
of mod_tile is only installed on a single host by default.
To install on multiple hosts either use ServerAlias or
use the LoadTileConfigFile in each virtual host.


Performance
===========
mod_tile is designed for high performance tile serving. If the
underlying disk system allows it, it can easily provide > 10k tiles/s
on a single serve.

Rendering performance is mostly dependent on mapnik and postgis performance,
however renderd tries to make sure it uses underlying hardware as efficiently
as possible and scales well on multi core systems. Renderd also provides
built-in features to scale to multi server rendering set-ups.


