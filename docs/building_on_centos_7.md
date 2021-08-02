# Building on CentOS 7

As `CentOS 7` does not provide any `mapnik`/`mapnik-devel` packages in the official repository (nor are any available from `EPEL`,) it must therefore be built and installed before `mod_tile` can be built. Although `boost-devel` is present in the official repository, the version available there (`1.53.0`) is not in [mapnik's recommended dependency list](https://github.com/mapnik/mapnik/blob/v3.0.24/INSTALL.md#depends), so the `boost169-devel` package from `EPEL` should probably be used instead.

```shell
#!/usr/bin/env bash
export LD_LIBRARY_PATH=/usr/local/lib
export MAPNIK_VERSION=3.0.24

# Install `EPEL` yum repository
yum --assumeyes install epel-release

# Update installed packages
yum --assumeyes update

# Install "Development Tools" group
yum --assumeyes groups install \
  "Development Tools"

# Install build dependencies
yum --assumeyes install \
  boost169-devel \
  cairo-devel \
  freetype-devel \
  gdal-devel \
  glib2-devel \
  harfbuzz-devel \
  httpd-devel \
  iniparser-devel \
  libcurl-devel \
  libicu-devel \
  libjpeg-turbo-devel \
  libmemcached-devel \
  libpng-devel \
  librados2-devel \
  libtiff-devel \
  libwebp-devel \
  libxml2-devel \
  postgresql-devel \
  proj-devel \
  sqlite-devel \
  zlib-devel

# Export `GDAL_DATA` & `PROJ_LIB` variables and create directories (if needed)
export GDAL_DATA=$(gdal-config --datadir)
export PROJ_LIB=/usr/share/proj
mkdir -p ${GDAL_DATA} ${PROJ_LIB}

# Download, Build & Install `Mapnik`
mkdir -p /usr/local/src/mapnik-${MAPNIK_VERSION}
cd /usr/local/src/mapnik-${MAPNIK_VERSION}
curl --silent --location https://github.com/mapnik/mapnik/releases/download/v${MAPNIK_VERSION}/mapnik-v${MAPNIK_VERSION}.tar.bz2 \
  | tar --verbose --extract --bzip2 --strip-components=1 --file=-
./configure BOOST_INCLUDES=/usr/include/boost169 BOOST_LIBS=/usr/lib64/boost169
JOBS=$(nproc) make
make install

# Fix issue with `iniparser.h` from `iniparser-devel` not being in the expected location
mkdir /usr/include/iniparser
ln -s /usr/include/iniparser.h /usr/include/iniparser/iniparser.h

# Download, Build & Install `mod_tile`
git clone https://github.com/openstreetmap/mod_tile.git /usr/local/src/mod_tile
cd /usr/local/src/mod_tile
./autogen.sh
./configure
make
make install
make install-mod_tile
```
