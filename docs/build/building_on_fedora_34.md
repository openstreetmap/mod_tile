# Building on Fedora 34

This documents step by step on how to compile and put into use the software `mod_tile` and `renderd`.
Please see our [Continous Integration script](../../.github/workflows/build-and-test-fedora-34.yml) for more detail.

```shell
#!/usr/bin/env bash

# Update installed packages
sudo yum --assumeyes update

# Install "Development Tools" group
sudo yum --assumeyes groups install \
  "Development Tools" \
  "C Development Tools and Libraries" \
  "Development Libraries" \
  "Development Tools"

# Install build dependencies
sudo yum --assumeyes install \
  cairo-devel \
  glib2-devel \
  httpd-devel \
  iniparser-devel \
  mapnik-devel \
  libcurl-devel \
  libmemcached-devel \
  librados-devel

# Download, Build & Install `mod_tile`
git clone https://github.com/openstreetmap/mod_tile.git /usr/local/src/mod_tile
cd /usr/local/src/mod_tile
./autogen.sh
./configure
make

# Create tiles directory
sudo mkdir --parents /run/renderd /var/cache/renderd/tiles

# Move files of example map
sudo cp -r "utils/example-map" /var/www/example-map

# Install leaflet
sudo curl --silent \
  "https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.7.1/leaflet.js" \
  > /var/www/example-map/leaflet/leaflet.min.js
sudo curl --silent \
  "https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.7.1/leaflet.css" \
  > /var/www/example-map/leaflet/leaflet.css

# Add configuration
sudo cp "etc/renderd/renderd.conf.examples" /etc/renderd.conf
sudo cp "etc/apache2/renderd.conf" /etc/httpd/conf.d/renderd.conf
sudo cp "apache2/renderd-example-map.conf" \
  /etc/httpd/conf.d/renderd-example-map.conf

# Apply Fedora specific changes to configuration files
sudo sed --in-place \
  "s#/usr/lib/mapnik/3.0/input#/usr/lib64/mapnik/input#g" \
  /etc/renderd.conf
sudo sed --in-place \
  "s#/usr/share/fonts/truetype#/usr/share/fonts#g" \
  /etc/renderd.conf

# Add and activate mod_tile for Apache
echo "LoadModule tile_module /usr/lib64/httpd/modules/mod_tile.so" \
  | sudo tee --append /etc/httpd/conf.modules.d/11-mod_tile.conf

# Make example map the new main page of Apache
sudo rm --force /etc/httpd/conf.d/welcome.conf

# Install software
sudo make install
sudo make install-mod_tile

# Start services
sudo httpd
sudo renderd -f
```

Then you can visit: `http://localhost/example-map`
