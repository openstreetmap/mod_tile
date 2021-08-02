# Building on CentOS 7

As `CentOS 7` does not provide `mapnik`/`mapnik-devel` in the official repository (nor is it available from EPEL,) it must therefore be built and installed before `mod_tile` can be built. Although `boost` is not required (since `boost-devel` exists in the official repository,) the version available there (`1.53.0`) is not in the [recommended dependency list](https://github.com/mapnik/mapnik/blob/master/INSTALL.md#depends). There is a version of `boost` available from EPEL that might work though (`boost169-devel`.)

```shell
#!/usr/bin/env bash
export BOOST_VERSION=1.69.0
export MAPNIK_VERSION=3.0.24
export LD_LIBRARY_PATH=/usr/local/lib

# Install EPEL & PGDG yum repositories
yum --assumeyes install \
  epel-release \
  https://download.postgresql.org/pub/repos/yum/reporpms/EL-7-x86_64/pgdg-redhat-repo-latest.noarch.rpm

# Update installed packages
yum --assumeyes update

# Install "Development Tools" group
yum --assumeyes groups install \
  "Development Tools"

# Install build dependencies
yum --assumeyes install \
  bzip2-devel \
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
  proj-devel \
  python-devel \
  sqlite-devel

# Download, Build & Install `Boost`
mkdir -p /usr/local/src/boost-${BOOST_VERSION}
cd /usr/local/src/boost-${BOOST_VERSION}
curl --silent --location https://boostorg.jfrog.io/artifactory/main/release/${BOOST_VERSION}/source/boost_$(echo ${BOOST_VERSION} | tr . _).tar.bz2 \
  | tar --verbose --extract --bzip2 --strip-components=1 --file=-
./bootstrap.sh
./b2 -d0 -j$(nproc) \
  --with-filesystem --with-program_options --with-python \
  --with-regex --with-system --with-thread \
  release install

# Download, Build & Install `Mapnik`
mkdir -p /usr/local/src/mapnik-${MAPNIK_VERSION}
cd /usr/local/src/mapnik-${MAPNIK_VERSION}
curl --silent --location https://github.com/mapnik/mapnik/releases/download/v${MAPNIK_VERSION}/mapnik-v${MAPNIK_VERSION}.tar.bz2 \
  | tar --verbose --extract --bzip2 --strip-components=1 --file=-
./configure
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
