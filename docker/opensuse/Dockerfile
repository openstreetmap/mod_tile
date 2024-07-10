# hadolint global ignore=DL3025,DL3036,DL3037,DL3059
# Arguments
ARG boost_version
ARG gcc_version
ARG mapnik_version=4.0.0
ARG opensuse_release=leap
ARG opensuse_version=15

# Mapnik Builder
FROM opensuse/${opensuse_release}:${opensuse_version} AS mapnik-builder

## Arguments
ARG boost_version
ARG gcc_version
ARG mapnik_version
ARG opensuse_release
ARG opensuse_version

## Install mapnik-builder dependencies
RUN --mount=type=cache,sharing=locked,id=opensuse/${opensuse_release}:${opensuse_version}-/var/cache/zypp,target=/var/cache/zypp \
    zypper modifyrepo --all --keep-packages && \
    zypper --non-interactive update && \
    zypper --non-interactive install \
        cairo-devel \
        cmake \
        freetype-devel \
        gcc${gcc_version} \
        gcc${gcc_version}-c++ \
        gdal-devel \
        git \
        harfbuzz-devel \
        libboost_regex${boost_version}-devel \
        libicu-devel \
        libjpeg8-devel \
        libpng16-devel \
        libtiff-devel \
        libwebp-devel \
        libxml2-devel \
        postgresql-devel \
        proj-devel \
        sqlite3-devel \
        zlib-devel && \
    if [ -n "${gcc_version}" ]; then \
        update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-${gcc_version} 10; \
        update-alternatives --install /usr/bin/cc gcc /usr/bin/gcc-${gcc_version} 10; \
        update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-${gcc_version} 10; \
        update-alternatives --install /usr/bin/c++ g++ /usr/bin/g++-${gcc_version} 10; \
    fi

## Download, Build & Install `Mapnik`
WORKDIR /tmp/mapnik_src
RUN --mount=type=cache,id=opensuse/${opensuse_release}:${opensuse_version}-mapnik-src:${mapnik_version},target=/tmp/mapnik_src \
    if [ ! -f CMakeLists.txt ]; then \
        git clone --branch v${mapnik_version} --depth 1 --jobs 8 --recurse-submodules https://github.com/mapnik/mapnik.git /tmp/mapnik_src; \
    fi
WORKDIR /tmp/mapnik_build
RUN --mount=type=cache,id=opensuse/${opensuse_release}:${opensuse_version}-mapnik-src:${mapnik_version},target=/tmp/mapnik_src \
    --mount=type=cache,id=opensuse/${opensuse_release}:${opensuse_version}-mapnik-build:${mapnik_version},target=/tmp/mapnik_build \
    CMAKE_BUILD_PARALLEL_LEVEL="$(nproc)" && export CMAKE_BUILD_PARALLEL_LEVEL && \
    export DESTDIR=/tmp/mapnik && \
    cmake -B . -S /tmp/mapnik_src \
        -DBUILD_BENCHMARK:BOOL=OFF \
        -DBUILD_DEMO_CPP:BOOL=OFF \
        -DBUILD_DEMO_VIEWER:BOOL=OFF \
        -DBUILD_TESTING:BOOL=OFF \
        -DBUILD_UTILITY_GEOMETRY_TO_WKB:BOOL=OFF \
        -DBUILD_UTILITY_MAPNIK_INDEX:BOOL=OFF \
        -DBUILD_UTILITY_MAPNIK_RENDER:BOOL=OFF \
        -DBUILD_UTILITY_OGRINDEX:BOOL=OFF \
        -DBUILD_UTILITY_PGSQL2SQLITE:BOOL=OFF \
        -DBUILD_UTILITY_SHAPEINDEX:BOOL=OFF \
        -DBUILD_UTILITY_SVG2PNG:BOOL=OFF \
        -DCMAKE_BUILD_TYPE:STRING=Release \
        -DCMAKE_INSTALL_PREFIX:PATH=/usr && \
    cmake --build . && \
    cmake --install . --strip

# Builder
FROM opensuse/${opensuse_release}:${opensuse_version} AS builder

## Arguments
ARG boost_version
ARG opensuse_release
ARG opensuse_version

## Install builder dependencies
RUN --mount=type=cache,sharing=locked,id=opensuse/${opensuse_release}:${opensuse_version}-/var/cache/zypp,target=/var/cache/zypp \
    zypper modifyrepo --all --keep-packages && \
    zypper --non-interactive update && \
    zypper --non-interactive install \
        apache2-devel \
        cairo-devel \
        cmake \
        curl \
        gcc \
        gcc-c++ \
        gdal \
        glib2-devel \
        harfbuzz-devel \
        libboost_regex${boost_version}-devel \
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

## Copy files from builder(s)
### Mapnik
COPY --from=mapnik-builder /tmp/mapnik /

## Create `nobody` user and group
RUN id nobody || useradd --home-dir / --no-create-home --shell /usr/sbin/nologin --system --user-group nobody

## Build, Test & Install `mod_tile`
COPY . /tmp/mod_tile_src
WORKDIR /tmp/mod_tile_build
RUN CMAKE_BUILD_PARALLEL_LEVEL="$(nproc)" && export CMAKE_BUILD_PARALLEL_LEVEL && \
    cmake -B . -S /tmp/mod_tile_src \
        -DCMAKE_BUILD_TYPE:STRING=Release \
        -DCMAKE_INSTALL_LOCALSTATEDIR:PATH=/var \
        -DCMAKE_INSTALL_PREFIX:PATH=/usr \
        -DCMAKE_INSTALL_RUNSTATEDIR:PATH=/run \
        -DCMAKE_INSTALL_SYSCONFDIR:PATH=/etc \
        -DENABLE_TESTS:BOOL=ON && \
    cmake --build .
RUN CTEST_PARALLEL_LEVEL="$(nproc)" && export CTEST_PARALLEL_LEVEL && \
    ctest --output-on-failure
RUN export DESTDIR=/tmp/mod_tile && \
    cmake --install . --strip

# Runner
FROM opensuse/${opensuse_release}:${opensuse_version} AS runner

## Arguments
ARG boost_version
ARG opensuse_release
ARG opensuse_version

## Install runner dependencies
RUN --mount=type=cache,sharing=locked,id=opensuse/${opensuse_release}:${opensuse_version}-/var/cache/zypp,target=/var/cache/zypp \
    zypper modifyrepo --all --keep-packages && \
    zypper --non-interactive update && \
    zypper --non-interactive install \
        apache2 \
        apache2-utils \
        cairo-devel \
        gdal \
        harfbuzz-devel \
        libboost_regex${boost_version}-devel \
        libicu-devel \
        libiniparser-devel \
        libjpeg8-devel \
        libmemcached-devel \
        librados-devel \
        libtiff-devel \
        libwebp-devel \
        proj

## Copy files from builder(s)
### Mapnik
COPY --from=mapnik-builder /tmp/mapnik /
### mod_tile
COPY --from=builder /tmp/mod_tile /
COPY --chown=apache:apache --from=builder \
    /tmp/mod_tile_src/utils/example-map \
    /usr/share/renderd/example-map
COPY --from=builder \
    /tmp/mod_tile_src/etc/apache2/renderd-example-map.conf \
    /etc/apache2/conf.d/renderd-example-map.conf

## Add configuration
RUN printf '\n[example-map]\nMAXZOOM=20\nMINZOOM=0\nURI=/tiles/renderd-example\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf
RUN printf '\n[example-map-jpg]\nMAXZOOM=20\nMINZOOM=0\nTYPE=jpg image/jpeg jpeg\nURI=/tiles/renderd-example-jpg\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf
RUN printf '\n[example-map-png256]\nMAXZOOM=20\nMINZOOM=0\nTYPE=png image/png png256\nURI=/tiles/renderd-example-png256\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf
RUN printf '\n[example-map-png32]\nMAXZOOM=20\nMINZOOM=0\nTYPE=png image/png png32\nURI=/tiles/renderd-example-png32\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf
RUN printf '\n[example-map-webp]\nMAXZOOM=20\nMINZOOM=0\nTYPE=webp image/webp webp\nURI=/tiles/renderd-example-webp\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf

## Enable `mod_access_compat`
RUN sed -i 's/^APACHE_MODULES="actions/APACHE_MODULES="access_compat actions/g' /etc/sysconfig/apache2

## Start services
CMD apachectl -e debug -k start; \
    G_MESSAGES_DEBUG=${G_MESSAGES_DEBUG:-info} renderd --foreground
