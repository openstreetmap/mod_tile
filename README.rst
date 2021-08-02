====================
mod_tile and renderd
====================

This software contains two main pieces:

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

Dependencies
------------

* `GNU/Linux` Operating System (works best on Debian or Ubuntu)
* `Apache 2 HTTP webserver <https://httpd.apache.org/>`__
* `Mapnik <https://mapnik.org/>`__
* `Cairo 2D graphics library  <https://cairographics.org/>`__
* `Curl library (SSL variant) <https://curl.haxx.se/>`__
* `Iniparser library <https://github.com/ndevilla/iniparser>`__
* `GLib library <https://gitlab.gnome.org/GNOME/glib>`__

Installation
------------

Starting from the following operation systems and their versions:

* Debian 11 (Bullseye)
* Ubuntu 21.04 (Hirsute Hippo)

the software and all dependencies can be installed simply with:

::

    $ apt install libapache2-mod-tile renderd

These packages for **Debian** and **Ubuntu** are being maintained by
the `Debian GIS Team <https://wiki.debian.org/DebianGis>`__ in the respective
`repository <https://salsa.debian.org/debian-gis-team/libapache2-mod-tile>`__.

Compilation
-----------

You may want to compile this software yourself. Either for developing on it or
when using it on an operating system this is not being packaged for.

We prepared instructions for you on how to build the software on the following
distributions:

* `CentOS 7 <docs/build/building_on_centos_7.md>`__
* `Fedora 34 </docs/build/building_on_fedora_34.md>`__
* `Ubuntu 20.04 </docs/build/building_on_ubuntu_20_04.md>`__ (this should work as well for Debian 10 and later)

Configuration
-------------

After you either installed the software packages or copiled the software
yourself, you can continue with the configuration. For your convenience
example configuration files are distributed with the software packages and
located in the ``etc`` directory of this repository.

A very basic example-map and data can be found in the ``utils/example-map``
directory. For a simple test copy it over to ``/var/www/example-map``.

Copy the configuration files to their place, too:

::

    $ cp etc/renderd/renderd.conf /etc/renderd.conf
    $ cp etc/apache2/renderd.conf /etc/apache2/conf-available/renderd.conf
    $ cp etc/apache2/renderd-example-map.conf /etc/apache2/conf-available/renderd-example-map.conf

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

Make sure the ``/var/cache/renderd/tiles`` directory is writable by
the user running the renderd process.

Try loading a tile in your browser, e.g.

::

    http://localhost/renderd-example/tiles/0/0/0.png


You may edit ``/etc/renderd.conf`` to indicate the location of different
mapnik style sheets (up to ten) and the endpoints you wish to use to access
it.

It is recommended to checkout `switch2osm
<https://switch2osm.org/serving-tiles/>`__ for nice tutorials
on how to set up a full tile server like on  `OpenStreetMap.org
<https://www.openstreetmap.org/>`__, using this software together with a
`PostgreSQL <https://www.postgresql.org/>`__ database and data from
OpenStreetMap.


Details about ``renderd``: Tile rendering
-----------------------------------------

The rendering is implemented in a multithreaded process
called ``renderd`` which opens either a unix or tcp socket
and listens for requests to render tiles. It uses Mapnik
to render tiles using the rendering rules defined in
the configuration file ``/etc/renderd.conf``. Its configuration
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
at compile time, see: ``render_config.h``


Details about ``mod_tile``: Tile serving
----------------------------------------

An Apache module called ``mod_tile`` enhances the regular
Apache file serving mechanisms to provide:

1) When tiles have expired it requests the rendering daemon to render (or re-render) the tile.
2) Remapping of the file path to the hashed layout.
3) Prioritizes rendering requests depending on the available resources on the server and how out of date they are.
4) Use tile storage other than a plain posix file system. e.g it can store tiles in a ceph object store, or proxy them from another tile server.
5) Tile expiry. It estimates when the tile is next likely to be rendered and adds the appropriate HTTP cache expiry headers. This is a configurable heuristic.

To avoid problems with directories becoming too large and to avoid
too many tiny files. They store the rendered tiles in "meta tiles" in a
special hashed directory structure. These combine 8x8 actual tiles into a
single metatile file. This is a more efficient use of disk space and inodes.

The metatiles are then stored in the following directory structure:
``/[base_dir]/[TileSetName]/[Z]/[xxxxyyyy]/[xxxxyyyy]/[xxxxyyyy]/[xxxxyyyy]/[xxxxyyyy].png``

Where ``base_dir`` is a configurable base path for all tiles. ``TileSetName``
is the name of the style sheet rendered. ``Z`` is the zoom level.
``[xxxxyyyy]`` is an 8 bit number, with the first 4 bits taken from the x
coordinate and the second 4 bits taken from the y coordinate. This
attempts to cluster 16x16 square of tiles together into a single sub
directory for more efficient access patterns.

Apache serves the files as if they were present under
``/[TileSetName]/Z/X/Y.png`` with the path being converted automatically.

Notes about performance
-----------------------

``mod_tile`` is designed for high performance tile serving. If the
underlying disk system allows it, it can easily provide > 10k tiles/s
on a single serve.

Rendering performance is mostly dependent on mapnik and postgis performance,
however ``renderd`` tries to make sure it uses underlying hardware as
efficiently as possible and scales well on multi core systems. ``renderd``
also provides built-in features to scale to multi server rendering set-ups.

Copyright and copyleft
----------------------

Copyright (c) 2007 - 2021 by mod_tile contributors (see `AUTHORS <./AUTHORS>`__)

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
