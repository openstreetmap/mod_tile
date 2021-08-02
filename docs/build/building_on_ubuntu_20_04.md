# Building on Ubuntu 20.04

This documents step by step on how to compile and put into use the software `mod_tile` and `renderd`.
Please see our [Continous Integration script](../../.github/workflows/build-and-test-ubuntu-20-04.yml) for more detail.

```shell
#!/usr/bin/env bash

# Update installed packages
sudo apt update && sudo apt upgrade --yes

# Install build dependencies
# (the last two are optional)
sudo apt install build-essential \
  autoconf \
  apache2-dev \
  libcairo2-dev \
  libcurl4-gnutls-dev \
  libglib2.0-dev \
  libiniparser-dev \
  libmapnik-dev \
  libmemcached-dev \
  librados-dev

# Download, build & install
git clone https://github.com/openstreetmap/mod_tile.git /usr/local/src/mod_tile
cd /usr/local/src/mod_tile
./autogen.sh
./configure
make

# Create tiles directory
sudo mkdir --parents /run/renderd /var/cache/renderd/tiles

# Move files of example map
sudo cp -r "utils/example-map" /var/www/example-map

# Link leaflet library
sudo ln --symbolic \
  /usr/share/javascript/leaflet \
  /var/www/example-map/leaflet

# Add configuration
sudo cp "etc/renderd/renderd.conf.examples" /etc/renderd.conf
sudo cp "etc/apache2/renderd.conf" /etc/apach2/conf.d/renderd.conf
sudo cp "etc/apache2/renderd-example-map.conf" \
  /etc/apache2/conf-enabled/renderd-example-map.conf

# Add and activate mod_tile for Apache
echo "LoadModule tile_module /usr/lib/apache2/modules/mod_tile.so" \
  | sudo tee --append /etc/apache2/mods-enabled/mod_tile.load

# Install software
sudo make install
sudo make install-mod_tile

# Start services
sudo systemctl --now enable apache2
sudo renderd -f
```

Then you can visit: `http://localhost/example-map`