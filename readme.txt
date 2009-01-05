mod_tile
========
A program to efficiently render and serve map tiles for
www.openstreetmap.org map using Apache and Mapnik.

Note: This program is very much still in development
it has numerous hard coded paths and options which need
to be made user configurable options. You may need to 
these to fit your local environment.

Requirements
============
OSM map data imported into PostgreSQL using osm2pgsql
Mapnik renderer along with the OSM.xml file and map
symbols, world_boundaries shapefiles. Apache with
development headers for APR module development.

Updating From Previous Version
==============================
The new version of mod_tile uses a slightly different
directory heirarchy from the previous version.  In order
to preserve the previously built tiles you will need to
move them to:

/var/lib/mod_tile/Default/[Z]/nnn/nnn/nnn/nnn/nnn.png

You will also need to recongiure your http configuration
to use the new apache directives and to create an 
/etc/renderd.conf file. See the example renderd.conf and
mod_tile.conf for details.

Tile Rendering
==============
The rendering is implemented in a multithreaded process
called renderd which opens a unix socket and listens for
requests to render tiles. It uses Mapnik to render tiles
using the rendering rules defined in the configuration file
/etc/renderd.conf

The render daemon implements a queueing mechanism which
can render foreground requests (for new tiles being viewed)
and background requests (updating tiles which have expired).
The size of the queue and the number of threads is determined
at compile time, see: render_config.h

Tile serving
============
To avoid problems with directories becoming too large the
files are stored in a different layout to that presented
by the web server. The tiles are now stored under
/var/lib/mod_tile/[TileSetName]/[Z]/nnn/nnn/nnn/nnn/nnn.png

Where nnn is derived from a combination of the X and Y
OSM tile co-ordinates.

Apache serves the files as if they were present
under "/osm_tiles2/Z/X/Y.png" with the path being
converted automatically.

An Apache module called mod_tile enhances the regular
Apache file serving mechanisms to provide:

1) Tile expiry. It estimates when the tile is next
likely to be rendered and adds the approriate HTTP
cache expiry headers

2) When tiles have expired it requests the rendering
daemon to render (or re-render) the tile.

3) Remapping of the file path to the hashed layout

There is an attempt to make the mod_tile code aware of
the load on the server so that it backs off the rendering
if the machine is under heavy load.

Setup
=====
Make sure you've read and implemented the things in the
requirements section. Edit the paths in the source to
match your local setup. Compile the code with make, and
then make install (as root, to copy the mod_tile to the
apache module directory).

Create a new apache config file to load the module,
e.g.

/etc/httpd/conf.d/mod_tile.conf

See the sample mod_tile.conf for details

Edit /etc/renderd.conf to indicate the location of your
mapnik style sheet and the uri you wish to use to access
it.  You may configure up to 10 (by default) mapnik
stylesheets - simply give each section a unique name and
enter the uri and style sheet path.

Make sure the /var/lib/mod_tile directory is writeable by 
the user running the renderd process and create a file an
empty file planet-import-complete in this folder.

Run the rendering daemon 'renderd'

Restart Aapche

Note: SELinux will prevent the mod_tile code from opening
the unix-socket to the render daemon so must be disabled.

Try loading a tile in your browser, e.g.
http://localhost/osm_tiles2/0/0/0.png

The render daemon should have produce a message like:

Got incoming connection, fd 7, number 1
Render fd(7) xml(Default), z(0), x(0), y(0)

The disk should start thrashing as Mapnik tries to pull
in data for the first time. After a few seconds you'll
probably see a 404 error. Wait for the disk activity to
cease and then reload the tile. With a bit of luck you
should see a tile of the world in your browser window.

If this fails to happen check the http error log.  You can 
increate the level of debuging using the LogLevel apache
directive.  If no log messages are shown check that you
are accessing the correct virtual host - the new version
of mod_tile is only installed on a single host by default.
To install on multiple hosts either use ServerAlias or
use the LoadTileConfigFile in each virtual host.

To get a complete slippy map you should install a copy
of the OpenLayers based OSM slippy map and point this to
fetch tiles from http://localhost/osm_tiles2

mysql2file
==========
This was written to export the existing OSM tiles from
the Mysql database to the filesystem.

Bugs
====
Too many hard coded options (need to be come module options or command
line options to renderd).
mod_tile uses many non-APR routines. It probably only works in Linux.
If rendering daemon dies then all queued rendering requests are lost.
Code has not been thoroughly tested.

Performance
===========
The existing tile serving based on Apache + mod_ruby + cat_tile.rb
+ Mysql manages to serve something in the region of 250 - 500 requests
per second. Apache + mod_tile manages 2000+ per second. Both these
figures are for tiles which have already been rendered.

Filesystem Issues
=================
The average tile size is currently somewhere in the region of 2.5kB.
(Based on a 20GB MySQL DB which contains 8M tiles). Typically
filesystems are not particularly efficient at storing large numbers
of small files. They often take a minimum of 4kB on the disk.

Unfortunately if you reduce the block size to 1 or 2kB then this also
has a significant impact on the maximum file system size and number of
inodes available.


**  Note: The issues below have been worked around in the current
code by using the hashed directory path. 

The simple z/x/y.png filesystem layout means that at high zoom levels
there can be large numbers of files in a single directory
 
  Zoom 18 = 2^18 = 256k files in a single directory.

If ext2/3 is being used then you really need to have directory indexing
enabled.

New ".meta" tile storage
========================
The latest code stores each metatile in a single .meta file instead of
lots of small .png files. This is a more efficient use of disk space
and inodes. For example, many sea tiles are 103 bytes long. In the old
scheme a meta tile of blank sea tiles would take 64 inodes of 4kB each,
a total of 256kB. In the new scheme it needs a single file of about 7kB.

The utility convert_meta can be used to convert a tree of .png files to
.meta (or back again).

mod_tile has been reworked to integrate more closely with Apache and
deliver tiles from the .meta files.
