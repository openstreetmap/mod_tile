# hadolint global ignore=DL3008,DL3025,DL3059
# Arguments
ARG libmapnik_version=3.1
ARG runner_additional_packages
ARG ubuntu_version=24.04

# Builder
FROM ubuntu:${ubuntu_version} AS builder

## Arguments
ARG ubuntu_version

## Install builder dependencies
RUN --mount=type=cache,sharing=locked,id=ubuntu:${ubuntu_version}-/var/cache/apt,target=/var/cache/apt \
    --mount=type=cache,sharing=locked,id=ubuntu:${ubuntu_version}-/var/lib/apt,target=/var/lib/apt \
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
FROM ubuntu:${ubuntu_version} AS runner

## Arguments
ARG libmapnik_version
ARG runner_additional_packages
ARG ubuntu_version

## Install runner dependencies
RUN --mount=type=cache,sharing=locked,id=ubuntu:${ubuntu_version}-/var/cache/apt,target=/var/cache/apt \
    --mount=type=cache,sharing=locked,id=ubuntu:${ubuntu_version}-/var/lib/apt,target=/var/lib/apt \
    export DEBIAN_FRONTEND=noninteractive && \
    apt-get --yes update && \
    apt-get --yes upgrade && \
    apt-get --no-install-recommends --yes install ${runner_additional_packages} \
        apache2 \
        libcairo2 \
        libcurl4 \
        libglib2.0-0 \
        libiniparser1 \
        libmapnik${libmapnik_version} \
        libmemcached11 \
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
