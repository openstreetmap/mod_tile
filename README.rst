====================
mod_tile and renderd
====================

This Software contains two main pieces:

1) ``mod_tile``: An Apache 2 module to deliver map tiles.
2) ``renderd``: A daemon that renders map tiles using mapnik.

.. figure:: ./screenshot.jpg
   :alt: Image shoing example slippy map and OSM layer

Together they efficiently render and serve raster map tiles for example
to use within a slippy map. The two consist of the classic raster tile
stack from `OpenStreetMap.org <https://openstreetmap.org>`__.

As an alternative to ``renderd`` its drop-in replacement
`Tirex <https://github.com/openstreetmap/tirex>`__ can be used in
combination with ``mod_tile``.

Requirements
------------

* `GNU/Linux` Operating System (works best on Debian or Ubuntu)
* `Apache 2 HTTP webserver <https://httpd.apache.org/>`__
* `Mapnik <https://mapnik.org/>`__
* `Cairo 2D graphics library  <https://cairographics.org/>`__
* `Curl library (SSL variant) <https://curl.haxx.se/>`__
* `Iniparser library <https://github.com/ndevilla/iniparser>`__
* `GLib library <https://gitlab.gnome.org/GNOME/glib>`__

Compilation
-----------

On Debian or Ubuntu systems the following packages are needed to start
compiling. On other systems the name of the libary might differ
slightly.

::

    $ apt install build-essential \
        autoconf \
        apache2-dev \
        libcairo2-dev \
        libcurl4-gnutls-dev \
        libglib2.0-dev \
        libiniparser-dev \
        libmapnik-dev

*(You may install more and optional dependencies in order to add
functionality to renderd)*

Once the dependencies are installed you can compile and install
mod_tile and renderd:

::

    $ ./autogen.sh
    $ ./configure
    $ make
    $ sudo make install
    $ sudo make install-mod_tile

Packages
--------

If you don't want to compile the software yourself.  Precompiled
software packages for **Debian** and **Ubuntu** are being maintained by
the `Debian GIS Team <https://wiki.debian.org/DebianGis>`__ in the respective
`repository <https://salsa.debian.org/debian-gis-team/libapache2-mod-tile>`__.
They are in the pipeline and expected to be included in the next releases of
those two distributions.

In the meantime an experimental repository can be used by adding the
corresponding line to your ``/etc/apt/sources.list``:

``deb https://deb.openbuildingmap.org/ bullseye main`` (for Debian Testing, Ubuntu Focal and Ubuntu Groovy)
``deb https://deb.openbuildingmap.org/ buster main`` (for Debian Stable)

Afterwards import the public key for the repository:

::

    $ wget -O - https://deb.openbuildingmap.org/archive.key | sudo apt-key add -

After that you can install the available software as any other package:

::

    $ apt update
    $ apt install libapache2-mod-tile renderd

Configuration
-------------

After you either copiled the software yourself or installed the software
packages, you can continue with the configuration. Here we assume that you have
installed mod_tile using packages from Debian/Ubuntu. If you compiled from
source you need to place debian-style configuration files at the relevant locations
to use the Debian helper scripts a2enmod/a2enconf, or manually create apache configuration.

For your convenience example configuration files are located in
the `etc` directory of this repository.

A very basic example-map and data can be found in the `example-map`
directory. For a simple test copy it over to ``/var/www/example-map``.

Copy the configuration files to their place:

::

    $ cp etc/renderd.conf.dist /etc/renderd.conf
    $ cp etc/apache2/renderd.conf.dist /etc/apache2/conf-available/renderd.conf
    $ cp etc/apache2/renderd-example-map.conf.dist /etc/apache2/conf-available/renderd-example-map.conf

Enable the configuration:

::

    $ sudo a2enmod tile
    $ sudo a2enconf renderd
    $ sudo a2enconf renderd-example-map

Restart apache2:

::

    $ sudo a2enmod tile
    $ sudo a2enconf renderd


And run the rendering daemon

::

    $ renderd -f

Make sure the /var/cache/renderd/tiles directory is writable by
the user running the renderd process.

Try loading a tile in your browser, e.g.

::

    http://localhost/renderd-example/tiles/0/0/0.png


You may edit /etc/renderd.conf to indicate the location of your
mapnik style sheet and the uri you wish to use to access it.  You may
configure up to 10 (by default) mapnik style sheets - simply give each
section a unique name and enter the uri and style sheet path.



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

For an OSM type setup, OSM map data imported into
`PostgreSQL <https://www.postgresql.org/>`__ using
`osm2pgsql <https://github.com/openstreetmap/osm2pgsql>`__ is needed.
Together with the Mapnik renderer along with the OSM.xml file and map
symbols, world_boundaries shapefiles.

Tile Rendering
--------------

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
------------

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

Performance
-----------

mod_tile is designed for high performance tile serving. If the
underlying disk system allows it, it can easily provide > 10k tiles/s
on a single serve.

Rendering performance is mostly dependent on mapnik and postgis performance,
however renderd tries to make sure it uses underlying hardware as efficiently
as possible and scales well on multi core systems. Renderd also provides
built-in features to scale to multi server rendering set-ups.

Copyright and copyleft
----------------------

Copyright (c) 2007 - 2020 by mod_tile contributors (see `AUTHORS <./AUTHORS>`__)

This program is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation, either version 2 of the License, or (at your
option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see http://www.gnu.org/licenses/.

See the `COPYING <./COPYING>`__ for the full license text.
