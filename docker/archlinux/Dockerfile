# Arguments
ARG runner_additional_packages

# Builder
FROM archlinux:latest as builder

## Install builder dependencies
RUN --mount=id=archlinux:latest-/var/cache/pacman/pkg,sharing=locked,target=/var/cache/pacman/pkg,type=cache \
    --mount=id=archlinux:latest-/var/lib/pacman/sync,sharing=locked,target=/var/lib/pacman/sync,type=cache \
    pacman --sync --refresh --sysupgrade --noconfirm \
        apache \
        apr \
        boost \
        cairo \
        cmake \
        curl \
        extra-cmake-modules \
        gcc \
        git \
        glib2 \
        iniparser \
        lcov \
        libmemcached \
        make \
        mapnik \
        memcached \
        pkgconf

## Build, Test & Install `mod_tile`
COPY . /tmp/mod_tile_src
WORKDIR /tmp/mod_tile_build
RUN export CMAKE_BUILD_PARALLEL_LEVEL=$(nproc) && \
    cmake -B . -S /tmp/mod_tile_src \
        -DCMAKE_BUILD_TYPE:STRING=Release \
        -DCMAKE_INSTALL_LOCALSTATEDIR=/var \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_INSTALL_RUNSTATEDIR=/run \
        -DCMAKE_INSTALL_SYSCONFDIR=/etc \
        -DENABLE_TESTS:BOOL=ON && \
    cmake --build .
RUN export CTEST_PARALLEL_LEVEL=$(nproc) && \
    export DESTDIR=/tmp/mod_tile && \
    ctest --output-on-failure && \
    (cmake --install . --strip || make DESTDIR=${DESTDIR} install/strip)

# Runner
FROM archlinux:latest as runner

## Arguments
ARG runner_additional_packages

## Install runner dependencies
RUN --mount=id=archlinux:latest-/var/cache/pacman/pkg,sharing=locked,target=/var/cache/pacman/pkg,type=cache \
    --mount=id=archlinux:latest-/var/lib/pacman/sync,sharing=locked,target=/var/lib/pacman/sync,type=cache \
    pacman --sync --refresh --sysupgrade --noconfirm ${runner_additional_packages} \
        apache \
        cairo \
        curl \
        glib2 \
        iniparser \
        libmemcached \
        mapnik \
        memcached \
        # GDAL optional dependencies
        arrow \
        cfitsio \
        hdf5 \
        libheif \
        libjxl \
        mariadb-libs \
        netcdf \
        openexr \
        openjpeg2 \
        podofo \
        poppler

## Copy files from builder(s)
### mod_tile
COPY --from=builder /tmp/mod_tile /
COPY --from=builder \
    /tmp/mod_tile_src/utils/example-map \
    /usr/share/renderd/example-map
COPY --from=builder \
    /tmp/mod_tile_src/etc/apache2/renderd-example-map.conf \
    /etc/httpd/conf/extra/httpd-tile-renderd-example-map.conf

## Add configuration
RUN printf '\n[example-map]\nMAXZOOM=20\nMINZOOM=0\nURI=/tiles/renderd-example\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf
RUN printf '\n[example-map-jpg]\nMAXZOOM=20\nMINZOOM=0\nTYPE=jpg image/jpeg jpeg\nURI=/tiles/renderd-example-jpg\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf
RUN printf '\n[example-map-png256]\nMAXZOOM=20\nMINZOOM=0\nTYPE=png image/png png256\nURI=/tiles/renderd-example-png256\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf
RUN printf '\n[example-map-png32]\nMAXZOOM=20\nMINZOOM=0\nTYPE=png image/png png32\nURI=/tiles/renderd-example-png32\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf
RUN printf '\n[example-map-webp]\nMAXZOOM=20\nMINZOOM=0\nTYPE=webp image/webp webp\nURI=/tiles/renderd-example-webp\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf

## Enable module & site
RUN printf '\nInclude conf/extra/httpd-tile.conf\n' >> /etc/httpd/conf/httpd.conf && \
    printf '\nInclude conf/extra/httpd-tile-renderd-example-map.conf\n' >> /etc/httpd/conf/httpd.conf

## Start services
CMD httpd -e debug -k start; \
    G_MESSAGES_DEBUG=${G_MESSAGES_DEBUG:-info} renderd --foreground
