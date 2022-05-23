# Building on FreeBSD 13.0

This documents step by step on how to compile and put into use the software `mod_tile` and `renderd`.

```shell
#!/usr/bin/env bash

sudo pkg install \
  apache24 \
  boost-all \
  cairo \
  ceph14 \
  cmake \
  curl \
  freetype2 \
  gdal \
  glib \
  gmake \
  harfbuzz \
  icu \
  iniparser \
  libjpeg-turbo \
  libmemcached \
  libxml2 \
  png \
  proj \
  python38 \
  sqlite3 \
  tiff \
  webp \
  zlib-ng

mkdir ~/mapnik-3.0.24
cd ~/mapnik-3.0.24

curl --silent --location https://github.com/mapnik/mapnik/releases/download/v3.0.24/mapnik-v3.0.24.tar.bz2 |
  tar --verbose --extract --bzip2 --strip-components=1 --file=-

export PROJ_LIB=/usr/local/share/proj
export PYTHON=python3.8
bash configure \
  FAST=True \
  OPTIMIZATION=2 \
  HB_INCLUDES=/usr/local/include/harfbuzz \
  HB_LIBS=/usr/local/lib \
  ICU_INCLUDES=/usr/local/include \
  ICU_LIBS=/usr/local/lib \
  PROJ_INCLUDES=/usr/local/include/proj \
  PROJ_LIBS=/usr/local/lib
JOBS=4 gmake PYTHON=${PYTHON}
sudo gmake install PYTHON=${PYTHON}

cd ~
git clone --branch CMake https://github.com/hummeltech/mod_tile.git
cd mod_tile
cmake -S . -B build -DCMAKE_LIBRARY_PATH=/usr/local/lib
cmake --build build
ctest --build-run-dir build --test-dir build
cmake --build build --target install
```
