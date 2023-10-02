# Building on FreeBSD

This document provides users with step-by-step instructions on how to compile and use`mod_tile` and `renderd`.

Please see our [Continuous Integration script](/.github/workflows/build-and-test.yml) for more details.

## FreeBSD 12/13

```shell
#!/usr/bin/env sh

# Update installed packages
sudo pkg upgrade --yes

# Install build dependencies
# (libmemcached & ceph14 are optional)
sudo pkg install --yes \
  apache24 \
  cairo \
  ceph14 \
  cmake \
  coreutils \
  curl \
  git \
  glib \
  iniparser \
  libmemcached \
  mapnik \
  pkgconf

# Download, Build, Test & Install `mod_tile`
export CMAKE_BUILD_PARALLEL_LEVEL=$(sysctl -n hw.ncpu)
export LIBRARY_PATH="/usr/local/lib"
rm -rf /tmp/mod_tile_src /tmp/mod_tile_build
mkdir /tmp/mod_tile_src /tmp/mod_tile_build
cd /tmp/mod_tile_src
git clone --depth 1 https://github.com/openstreetmap/mod_tile.git .
cd /tmp/mod_tile_build
cmake -B . -S /tmp/mod_tile_src \
  -DCMAKE_BUILD_TYPE:STRING=Release \
  -DENABLE_TESTS:BOOL=ON
cmake --build .
ctest
sudo cmake --install . --prefix /usr --strip

# Create /usr/share/renderd directory
sudo mkdir -p /usr/share/renderd

# Copy files of example map
sudo cp -av /tmp/mod_tile_src/utils/example-map /usr/share/renderd/example-map

# Add configuration
sudo cp -av /tmp/mod_tile_src/etc/apache2/renderd-example-map.conf /usr/local/etc/apache24/Includes/renderd-example-map.conf
printf '\n[example-map]\nURI=/tiles/renderd-example\nXML=/usr/share/renderd/example-map/mapnik.xml\n' | sudo tee -a /etc/renderd.conf

# Start services
sudo httpd
sudo renderd -f
```

Then you can visit: `http://localhost:8081/renderd-example-map`
