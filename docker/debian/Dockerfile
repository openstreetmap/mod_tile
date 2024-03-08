# Arguments
ARG debian_version=12
ARG libmapnik_version=3.1

# Builder
FROM debian:${debian_version} as builder

## Arguments
ARG debian_version

## Install builder dependencies
RUN --mount=id=debian:${debian_version}-/var/cache/apt,sharing=locked,target=/var/cache/apt,type=cache \
    --mount=id=debian:${debian_version}-/var/lib/apt,sharing=locked,target=/var/lib/apt,type=cache \
    export DEBIAN_FRONTEND=noninteractive && \
    apt-get --yes update && \
    apt-get --yes upgrade && \
    apt-get --no-install-recommends --yes install \
        apache2 \
        apache2-dev \
        cmake \
        curl \
        g++ \
        gcc \
        libcairo2-dev \
        libcurl4-openssl-dev \
        libglib2.0-dev \
        libiniparser-dev \
        libmapnik-dev \
        libmemcached-dev \
        librados-dev \
        netbase

## Build, Test & Install `mod_tile`
COPY . /tmp/mod_tile_src
WORKDIR /tmp/mod_tile_build
RUN export CMAKE_BUILD_PARALLEL_LEVEL=$(nproc) && \
    cmake -B . -S /tmp/mod_tile_src \
        -DCMAKE_BUILD_TYPE:STRING=Release \
        -DENABLE_TESTS:BOOL=ON && \
    cmake --build .
RUN export CTEST_PARALLEL_LEVEL=$(nproc) && \
    export DESTDIR=/tmp/mod_tile && \
    ctest --output-on-failure && \
    (cmake --install . --prefix /usr --strip || make DESTDIR=${DESTDIR} install/strip) && \
    mv /tmp/mod_tile/var/run /tmp/mod_tile/run

# Runner
FROM debian:${debian_version} as runner

## Arguments
ARG debian_version
ARG libmapnik_version

## Install runner dependencies
RUN --mount=id=debian:${debian_version}-/var/cache/apt,sharing=locked,target=/var/cache/apt,type=cache \
    --mount=id=debian:${debian_version}-/var/lib/apt,sharing=locked,target=/var/lib/apt,type=cache \
    export DEBIAN_FRONTEND=noninteractive && \
    apt-get --yes update && \
    apt-get --yes upgrade && \
    apt-get --no-install-recommends --yes install \
        apache2 \
        libcairo2 \
        libcurl4 \
        libglib2.0 \
        libiniparser1 \
        libmapnik${libmapnik_version} \
        libmemcached11 \
        libmemcachedutil2 \
        librados2

## Copy files from builder(s)
### mod_tile
COPY --from=builder /tmp/mod_tile /
COPY --from=builder \
    /tmp/mod_tile_src/utils/example-map \
    /usr/share/renderd/example-map
COPY --from=builder \
    /tmp/mod_tile_src/etc/apache2/renderd-example-map.conf \
    /etc/apache2/sites-available/renderd-example-map.conf

## Add configuration
RUN printf '\n[example-map]\nMAXZOOM=20\nMINZOOM=0\nURI=/tiles/renderd-example\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf
RUN printf '\n[example-map-jpg]\nMAXZOOM=20\nMINZOOM=0\nTYPE=jpg image/jpeg jpeg\nURI=/tiles/renderd-example-jpg\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf
RUN printf '\n[example-map-png256]\nMAXZOOM=20\nMINZOOM=0\nTYPE=png image/png png256\nURI=/tiles/renderd-example-png256\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf
RUN printf '\n[example-map-png32]\nMAXZOOM=20\nMINZOOM=0\nTYPE=png image/png png32\nURI=/tiles/renderd-example-png32\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf
RUN printf '\n[example-map-webp]\nMAXZOOM=20\nMINZOOM=0\nTYPE=webp image/webp webp\nURI=/tiles/renderd-example-webp\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf

## Enable module & site
RUN a2enmod tile && \
    a2ensite renderd-example-map

## Start services
CMD apachectl -e debug -k start; \
    G_MESSAGES_DEBUG=${G_MESSAGES_DEBUG:-info} renderd --foreground
