# Building on macOS

This document provides users with step-by-step instructions on how to compile and use`mod_tile` and `renderd`.

Please see our [Continuous Integration script](/.github/workflows/build-and-test.yml) for more details.

## macOS 11/12/13/14

```shell
#!/usr/bin/env bash

# Update installed packages
brew upgrade

# Install build dependencies
# (libmemcached is optional)
brew install \
  apr \
  cairo \
  cmake \
  coreutils \
  curl \
  glib \
  httpd \
  iniparser \
  libmemcached \
  mapnik \
  pkg-config

# Download, Build, Test & Install `mod_tile`
export CMAKE_BUILD_PARALLEL_LEVEL=$(nproc)
export CPATH=$(brew --prefix)/include
export ICU_ROOT=$(brew --prefix icu4c)
export LDFLAGS="-undefined dynamic_lookup"
export LIBRARY_PATH=$(brew --prefix)/lib
rm -rf /tmp/mod_tile_src /tmp/mod_tile_build
mkdir /tmp/mod_tile_src /tmp/mod_tile_build
cd /tmp/mod_tile_src
git clone --depth 1 https://github.com/openstreetmap/mod_tile.git .
cd /tmp/mod_tile_build
cmake -B . -S /tmp/mod_tile_src \
  -DCMAKE_BUILD_TYPE:STRING=Release \
  -DCMAKE_INSTALL_LOCALSTATEDIR:PATH=/var \
  -DCMAKE_INSTALL_PREFIX:PATH=/usr/local \
  -DCMAKE_INSTALL_RUNSTATEDIR:PATH=/var/run \
  -DCMAKE_INSTALL_SYSCONFDIR:PATH=/etc \
  -DENABLE_TESTS:BOOL=ON
cmake --build .
ctest
sudo cmake --install . --strip

# Create /usr/local/share/renderd directory
sudo mkdir -p /usr/local/share/renderd

# Copy files of example map
sudo cp -av /tmp/mod_tile_src/utils/example-map /usr/local/share/renderd/example-map

# Add configuration
sed 's#/usr/share/#/usr/local/share/#g' /tmp/mod_tile_src/etc/apache2/renderd-example-map.conf | sudo tee /usr/local/etc/httpd/extra/renderd-example-map.conf
printf '\nInclude /usr/local/etc/httpd/extra/httpd-tile.conf\nInclude /usr/local/etc/httpd/extra/renderd-example-map.conf\n' | sudo tee -a /usr/local/etc/httpd/httpd.conf
printf '\n[example-map]\nURI=/tiles/renderd-example\nXML=/usr/local/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf
printf '\n[example-map-jpg]\nTYPE=jpg image/jpeg jpeg\nURI=/tiles/renderd-example-jpg\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf
printf '\n[example-map-png256]\nTYPE=png image/png png256\nURI=/tiles/renderd-example-png256\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf
printf '\n[example-map-png32]\nTYPE=png image/png png32\nURI=/tiles/renderd-example-png32\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf
printf '\n[example-map-webp]\nTYPE=webp image/webp webp\nURI=/tiles/renderd-example-webp\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf

# Start services
httpd
sudo renderd -f
```

Then you can visit: `http://localhost:8081/renderd-example-map`
