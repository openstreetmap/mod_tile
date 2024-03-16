# Building on openSUSE

This document provides users with step-by-step instructions on how to compile and use`mod_tile` and `renderd`.

Please see our [Continuous Integration script](/.github/workflows/build-and-test.yml) for more details.

A Docker-based building & testing setup pipeline is also available [here](/docker) for your convenience.

_openSUSE does not provide a `mapnik`/`mapnik-devel` package, so it will first need to be built & installed, which is beyond the scope of this document, please visit the project's [installation document on GitHub](https://github.com/mapnik/mapnik/blob/master/INSTALL.md) or our [Continuous Integration script](/.github/actions/dependencies/build-and-install/mapnik/action.yml) for more information._

## openSUSE Leap 15/Tumbleweed

```shell
#!/usr/bin/env bash

# Update installed packages
sudo zypper --non-interactive update

# Install build dependencies
# (libmemcached-devel & librados-devel are optional)
sudo zypper --non-interactive install \
  apache2-devel \
  cairo-devel \
  cmake \
  curl \
  gcc \
  gcc-c++ \
  gdal \
  glib2-devel \
  harfbuzz-devel \
  libboost_atomic-devel \
  libboost_filesystem-devel \
  libboost_headers-devel \
  libboost_program_options-devel \
  libboost_regex-devel \
  libboost_system-devel \
  libcurl-devel \
  libicu-devel \
  libiniparser-devel \
  libjpeg8-devel \
  libmemcached-devel \
  librados-devel \
  libtiff-devel \
  libwebp-devel \
  libxml2-devel \
  proj-devel \
  tar

# Create `nobody` user and group
sudo useradd --home-dir / --no-create-home --shell /usr/sbin/nologin --system --user-group nobody

# Download, Build, Test & Install `mod_tile`
export CMAKE_BUILD_PARALLEL_LEVEL=$(nproc)
rm -rf /tmp/mod_tile_src /tmp/mod_tile_build
mkdir /tmp/mod_tile_src /tmp/mod_tile_build
cd /tmp/mod_tile_src
git clone --depth 1 https://github.com/openstreetmap/mod_tile.git .
cd /tmp/mod_tile_build
cmake -B . -S /tmp/mod_tile_src \
  -DCMAKE_BUILD_TYPE:STRING=Release \
  -DCMAKE_INSTALL_LOCALSTATEDIR=/var \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_RUNSTATEDIR=/run \
  -DCMAKE_INSTALL_SYSCONFDIR=/etc \
  -DENABLE_TESTS:BOOL=ON
cmake --build .
ctest
sudo cmake --install . --strip

# Create /usr/share/renderd directory
sudo mkdir --parents /usr/share/renderd

# Copy files of example map
sudo cp -av /tmp/mod_tile_src/utils/example-map /usr/share/renderd/example-map

# Add configuration
sudo cp -av /tmp/mod_tile_src/etc/apache2/renderd-example-map.conf /etc/apache2/conf.d/renderd-example-map.conf
printf '\n[example-map]\nURI=/tiles/renderd-example\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf
printf '\n[example-map-jpg]\nTYPE=jpg image/jpeg jpeg\nURI=/tiles/renderd-example-jpg\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf
printf '\n[example-map-png256]\nTYPE=png image/png png256\nURI=/tiles/renderd-example-png256\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf
printf '\n[example-map-png32]\nTYPE=png image/png png32\nURI=/tiles/renderd-example-png32\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf
printf '\n[example-map-webp]\nTYPE=webp image/webp webp\nURI=/tiles/renderd-example-webp\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf

# Enable `mod_access_compat`
sudo sed -i 's/^APACHE_MODULES="actions/APACHE_MODULES="access_compat actions/g' /etc/sysconfig/apache2

# Start services
sudo apache2ctl start
sudo renderd -f
```

Then you can visit: `http://localhost:8081/renderd-example-map`
