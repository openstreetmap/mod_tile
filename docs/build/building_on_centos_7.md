# Building on CentOS 7

This documents step by step on how to compile and put into use the software `mod_tile` and `renderd`.

As `CentOS 7` does not provide any `mapnik`/`mapnik-devel` packages in the official repository (nor are any available from `EPEL`,) it must therefore be built and installed before `mod_tile` can be built. Although `boost-devel` is present in the official repository, the version available there (`1.53.0`) is not in [mapnik's recommended dependency list](https://github.com/mapnik/mapnik/blob/v3.0.24/INSTALL.md#depends), so the `boost169-devel` package from `EPEL` should probably be used instead.

```shell
#!/usr/bin/env bash
export LD_LIBRARY_PATH=/usr/local/lib
export MAPNIK_VERSION=3.0.24

# Install `EPEL` yum repository
sudo yum --assumeyes install epel-release

# Update installed packages
sudo yum --assumeyes update

# Install "Development Tools" group
sudo yum --assumeyes groups install \
  "Development Tools"

# Install build dependencies
sudo yum --assumeyes install \
  boost169-devel \
  cairo-devel \
  cmake3 \
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
sudo --preserve-env mkdir -p ${GDAL_DATA} ${PROJ_LIB}

# Download, Build & Install `Mapnik`
sudo mkdir -p /usr/local/src/mapnik-${MAPNIK_VERSION}
cd /usr/local/src/mapnik-${MAPNIK_VERSION}
sudo curl --silent --location https://github.com/mapnik/mapnik/releases/download/v${MAPNIK_VERSION}/mapnik-v${MAPNIK_VERSION}.tar.bz2 \
  | sudo tar --verbose --extract --bzip2 --strip-components=1 --file=-
sudo --preserve-env ./configure BOOST_INCLUDES=/usr/include/boost169 BOOST_LIBS=/usr/lib64/boost169 FAST=True OPTIMIZATION=2
sudo --preserve-env JOBS=$(nproc) make install

# Download and build
sudo git clone https://github.com/openstreetmap/mod_tile.git /usr/local/src/mod_tile
cd /usr/local/src/mod_tile
sudo rm -rf build
sudo mkdir build
sudo cmake3 -B build
sudo make --directory build

# Create tiles directory
sudo mkdir --parents /run/renderd /var/cache/renderd/tiles

# Move files of example map
sudo cp -r "utils/example-map" /var/www/example-map

# Link example-map to /var/www/html
sudo ln --symbolic /var/www/example-map /var/www/html/

# Install leaflet
sudo mkdir /var/www/example-map/leaflet
sudo curl --silent \
  "https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.7.1/leaflet.js" \
  > /var/www/example-map/leaflet/leaflet.min.js
sudo curl --silent \
  "https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.7.1/leaflet.css" \
  > /var/www/example-map/leaflet/leaflet.css

# Add configuration
sudo cp "etc/renderd/renderd.conf.examples" /etc/renderd.conf
sudo cp "etc/apache2/renderd-example-map.conf" \
  /etc/httpd/conf.d/renderd-example-map.conf

# Apply CentOS specific changes to configuration files
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
sudo make --directory build install

# Start services
sudo httpd
sudo renderd -f
```

Then you can visit: `http://localhost/example-map`
