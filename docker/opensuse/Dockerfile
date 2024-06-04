# Arguments
ARG boost_version
ARG gcc_version
ARG opensuse_version=leap:15

# Mapnik Builder
FROM opensuse/${opensuse_version} as mapnik-builder

## Arguments
ARG boost_version
ARG gcc_version
ARG opensuse_version

## Install mapnik-builder dependencies
RUN --mount=id=opensuse:${opensuse_version}-/var/cache/zypp,target=/var/cache/zypp,type=cache,sharing=locked \
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
        libboost_atomic${boost_version}-devel \
        libboost_filesystem${boost_version}-devel \
        libboost_headers${boost_version}-devel \
        libboost_program_options${boost_version}-devel \
        libboost_regex${boost_version}-devel \
        libboost_system${boost_version}-devel \
        libicu-devel \
        libjpeg8-devel \
        libpng16-devel \
        libtiff-devel \
        libwebp-devel \
        libxml2-devel \
        postgresql-devel \
        proj-devel \
        python3 \
        sqlite3-devel \
        tar \
        zlib-devel && \
    if [ -n "${gcc_version}" ]; then \
        update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-${gcc_version} 10; \
        update-alternatives --install /usr/bin/cc gcc /usr/bin/gcc-${gcc_version} 10; \
        update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-${gcc_version} 10; \
        update-alternatives --install /usr/bin/c++ g++ /usr/bin/g++-${gcc_version} 10; \
    fi

## Download, Build & Install `Mapnik`
WORKDIR /tmp/mapnik_src
RUN --mount=id=opensuse:${opensuse_version}-mapnik-src:latest,target=/tmp/mapnik_src,type=cache \
    if [ ! -f CMakeLists.txt ]; then \
        git clone --depth 1 --jobs 8 --recurse-submodules https://github.com/mapnik/mapnik.git /tmp/mapnik_src; \
    fi
WORKDIR /tmp/mapnik_build
RUN --mount=id=opensuse:${opensuse_version}-mapnik-src:latest,target=/tmp/mapnik_src,type=cache \
    --mount=id=opensuse:${opensuse_version}-mapnik-build:latest,target=/tmp/mapnik_build,type=cache \
    export CMAKE_BUILD_PARALLEL_LEVEL=$(nproc) && \
    export DESTDIR=/tmp/mapnik && \
    cmake -B . -S /tmp/mapnik_src \
        -DBUILD_BENCHMARK:BOOL=OFF \
        -DBUILD_DEMO_CPP:BOOL=OFF \
        -DBUILD_DEMO_VIEWER:BOOL=OFF \
        -DBUILD_TESTING:BOOL=OFF \
        -DCMAKE_BUILD_TYPE:STRING=Release \
        -DCMAKE_CXX_STANDARD:STRING=17 \
        -DCMAKE_INSTALL_PREFIX:PATH=/usr \
        -DMAPNIK_PKGCONF_DIR:PATH=/usr/share/pkgconfig && \
    cmake --build . && \
    (cmake --install . --prefix /usr --strip || make DESTDIR=${DESTDIR} install/strip)

# Builder
FROM opensuse/${opensuse_version} as builder

## Arguments
ARG boost_version
ARG opensuse_version

## Install builder dependencies
RUN --mount=id=opensuse:${opensuse_version}-/var/cache/zypp,target=/var/cache/zypp,type=cache,sharing=locked \
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
        libboost_atomic${boost_version}-devel \
        libboost_filesystem${boost_version}-devel \
        libboost_headers${boost_version}-devel \
        libboost_program_options${boost_version}-devel \
        libboost_regex${boost_version}-devel \
        libboost_system${boost_version}-devel \
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
RUN export CMAKE_BUILD_PARALLEL_LEVEL=$(nproc) && \
    cmake -B . -S /tmp/mod_tile_src \
        -DCMAKE_BUILD_TYPE:STRING=Release \
        -DCMAKE_INSTALL_LOCALSTATEDIR:PATH=/var \
        -DCMAKE_INSTALL_PREFIX:PATH=/usr \
        -DCMAKE_INSTALL_RUNSTATEDIR:PATH=/run \
        -DCMAKE_INSTALL_SYSCONFDIR:PATH=/etc \
        -DENABLE_TESTS:BOOL=ON && \
    cmake --build .
RUN export CTEST_PARALLEL_LEVEL=$(nproc) && \
    export DESTDIR=/tmp/mod_tile && \
    ctest --output-on-failure && \
    (cmake --install . --strip || make DESTDIR=${DESTDIR} install/strip)

# Runner
FROM opensuse/${opensuse_version} as runner

## Arguments
ARG boost_version
ARG opensuse_version

## Install runner dependencies
RUN --mount=id=opensuse:${opensuse_version}-/var/cache/zypp,target=/var/cache/zypp,type=cache,sharing=locked \
    zypper modifyrepo --all --keep-packages && \
    zypper --non-interactive update && \
    zypper --non-interactive install \
        apache2 \
        apache2-utils \
        cairo-devel \
        gdal \
        harfbuzz-devel \
        libboost_atomic${boost_version}-devel \
        libboost_filesystem${boost_version}-devel \
        libboost_program_options${boost_version}-devel \
        libboost_regex${boost_version}-devel \
        libboost_system${boost_version}-devel \
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
