# Building on Debian

This document provides users with step-by-step instructions on how to compile and use`mod_tile` and `renderd`.

Please see our [Continuous Integration script](/.github/workflows/build-and-test.yml) for more details.

A Docker-based building & testing setup pipeline is also available [here](/docker) for your convenience.

## Debian 10/11/12

```shell
#!/usr/bin/env bash

# Update installed packages
sudo apt update && sudo apt --yes upgrade
sudo apt --yes install --reinstall ca-certificates

# Install build dependencies
# (libmemcached-dev & librados-dev are optional)
sudo apt --no-install-recommends --yes install \
  apache2 \
  apache2-dev \
  cmake \
  curl \
  g++ \
  gcc \
  git \
  libcairo2-dev \
  libcurl4-openssl-dev \
  libglib2.0-dev \
  libiniparser-dev \
  libmapnik-dev \
  libmemcached-dev \
  librados-dev

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
sudo cp -av /tmp/mod_tile_src/etc/apache2/renderd-example-map.conf /etc/apache2/sites-available/renderd-example-map.conf
printf '\n[example-map]\nURI=/tiles/renderd-example\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf
printf '\n[example-map-jpg]\nTYPE=jpg image/jpeg jpeg\nURI=/tiles/renderd-example-jpg\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf
printf '\n[example-map-png256]\nTYPE=png image/png png256\nURI=/tiles/renderd-example-png256\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf
printf '\n[example-map-png32]\nTYPE=png image/png png32\nURI=/tiles/renderd-example-png32\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf
printf '\n[example-map-webp]\nTYPE=webp image/webp webp\nURI=/tiles/renderd-example-webp\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf

# Enable configuration
a2enmod tile
a2ensite renderd-example-map

# Start services
sudo apache2ctl start
sudo renderd -f
```

Then you can visit: `http://localhost:8081/renderd-example-map`
