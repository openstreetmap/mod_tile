# Building on CentOS

This document provides users with step-by-step instructions on how to compile and use`mod_tile` and `renderd`.

Please see our [Continuous Integration script](/.github/workflows/build-and-test.yml) for more details.

_CentOS does not provide a `mapnik`/`mapnik-devel` package, so it will first need to be built & installed, which is beyond the scope of this document, please visit the project's [installation document on GitHub](https://github.com/mapnik/mapnik/blob/master/INSTALL.md) or our [Continuous Integration script](/.github/actions/dependencies/build-and-install/mapnik/action.yml) for more information._

## CentOS 7
```shell
#!/usr/bin/env bash

# Update installed packages
sudo yum --assumeyes update

# Install build dependencies
# (libmemcached-devel & librados2-devel are optional)
sudo yum --assumeyes install epel-release
sudo yum --assumeyes --setopt=install_weak_deps=False install \
  boost169-devel \
  cairo-devel \
  cmake3 \
  gcc \
  gcc-c++ \
  gdal \
  git \
  glib2-devel \
  harfbuzz-devel \
  httpd-devel \
  iniparser-devel \
  libcurl-devel \
  libicu-devel \
  libjpeg \
  libmemcached-devel \
  librados2-devel \
  libtiff \
  libwebp \
  make \
  proj

# Download, Build, Test & Install `mod_tile`
export CMAKE_BUILD_PARALLEL_LEVEL=$(nproc)
rm -rf /tmp/mod_tile_src /tmp/mod_tile_build
mkdir /tmp/mod_tile_src /tmp/mod_tile_build
cd /tmp/mod_tile_src
git clone --depth 1 https://github.com/openstreetmap/mod_tile.git .
cd /tmp/mod_tile_build
cmake3 -B . -S /tmp/mod_tile_src \
  -DCMAKE_BUILD_TYPE:STRING=Release \
  -DCMAKE_CXX_FLAGS:STRING="-I/usr/include/boost169" \
  -DCMAKE_C_FLAGS:STRING="-I/usr/include/boost169" \
  -DENABLE_TESTS:BOOL=ON
cmake3 --build .
ctest3
sudo cmake --install . --prefix /usr --strip

# Create /usr/share/renderd directory
sudo mkdir --parents /usr/share/renderd

# Copy files of example map
sudo cp -av /tmp/mod_tile_src/utils/example-map /usr/share/renderd/example-map

# Add configuration
sudo cp -av /tmp/mod_tile_src/etc/apache2/renderd-example-map.conf /etc/httpd/conf.d/renderd-example-map.conf
printf '\n[example-map]\nURI=/tiles/renderd-example\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf

# Start services
sudo httpd
sudo renderd -f
```

Then you can visit: `http://localhost:8081/renderd-example-map`
