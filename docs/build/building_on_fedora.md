# Building on Fedora

This document provides users with step-by-step instructions on how to compile and use`mod_tile` and `renderd`.

Please see our [Continuous Integration script](/.github/workflows/build-and-test.yml) for more details.

A Docker-based building & testing setup pipeline is also available [here](/docker) for your convenience.

## Fedora 34/35/36/37/38/39/40

```shell
#!/usr/bin/env bash

# Update installed packages
sudo dnf --assumeyes update

# Install build dependencies
# (libmemcached-devel & librados-devel are optional)
sudo dnf --assumeyes --setopt=install_weak_deps=False install \
  cairo-devel \
  cmake \
  gcc \
  gcc-c++ \
  git \
  glib2-devel \
  httpd-devel \
  iniparser-devel \
  libcurl-devel \
  libmemcached-devel \
  librados-devel \
  mapnik-devel \
  mapnik-static

# Download, Build, Test & Install `mod_tile`
export CMAKE_BUILD_PARALLEL_LEVEL=$(nproc)
rm -rf /tmp/mod_tile_src /tmp/mod_tile_build
mkdir /tmp/mod_tile_src /tmp/mod_tile_build
cd /tmp/mod_tile_src
git clone --depth 1 https://github.com/openstreetmap/mod_tile.git .
cd /tmp/mod_tile_build
cmake -B . -S /tmp/mod_tile_src \
  -DCMAKE_BUILD_TYPE:STRING=Release \
  -DCMAKE_INSTALL_LOCALSTATEDIR:PATH=/var \
  -DCMAKE_INSTALL_PREFIX:PATH=/usr \
  -DCMAKE_INSTALL_RUNSTATEDIR:PATH=/run \
  -DCMAKE_INSTALL_SYSCONFDIR:PATH=/etc \
  -DENABLE_TESTS:BOOL=ON
cmake --build .
ctest
sudo cmake --install . --strip

# Create /usr/share/renderd directory
sudo mkdir --parents /usr/share/renderd

# Copy files of example map
sudo cp -av /tmp/mod_tile_src/utils/example-map /usr/share/renderd/example-map

# Add configuration
sudo cp -av /tmp/mod_tile_src/etc/apache2/renderd-example-map.conf /etc/httpd/conf.d/renderd-example-map.conf
printf '\n[example-map]\nURI=/tiles/renderd-example\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf
printf '\n[example-map-jpg]\nTYPE=jpg image/jpeg jpeg\nURI=/tiles/renderd-example-jpg\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf
printf '\n[example-map-png256]\nTYPE=png image/png png256\nURI=/tiles/renderd-example-png256\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf
printf '\n[example-map-png32]\nTYPE=png image/png png32\nURI=/tiles/renderd-example-png32\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf
printf '\n[example-map-webp]\nTYPE=webp image/webp webp\nURI=/tiles/renderd-example-webp\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf

# Start services
sudo httpd
sudo renderd -f
```

Then you can visit: `http://localhost:8081/renderd-example-map`
