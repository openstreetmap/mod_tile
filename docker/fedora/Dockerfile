# hadolint global ignore=DL3025,DL3040,DL3041,DL3059
# Arguments
ARG fedora_version=40

# Builder
FROM fedora:${fedora_version} AS builder

## Arguments
ARG fedora_version

## Install builder dependencies
RUN --mount=type=cache,sharing=locked,id=fedora:${fedora_version}-/var/cache/dnf,target=/var/cache/dnf \
    echo "install_weak_deps=False" >> /etc/dnf/dnf.conf && \
    echo "keepcache=True" >> /etc/dnf/dnf.conf && \
    dnf --assumeyes install \
      cairo-devel \
      cmake \
      gcc \
      gcc-c++ \
      glib2-devel \
      httpd-devel \
      iniparser-devel \
      libcurl-devel \
      libmemcached-devel \
      librados-devel \
      mapnik-devel \
      mapnik-static \
      procps

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
FROM fedora:${fedora_version} AS runner

## Arguments
ARG fedora_version

## Install runner dependencies
RUN --mount=type=cache,sharing=locked,id=fedora:${fedora_version}-/var/cache/dnf,target=/var/cache/dnf \
    echo "install_weak_deps=False" >> /etc/dnf/dnf.conf && \
    echo "keepcache=True" >> /etc/dnf/dnf.conf && \
    dnf --assumeyes install \
      cairo \
      glib2 \
      httpd \
      iniparser \
      libcurl \
      libmemcached \
      librados2 \
      mapnik

## Copy files from builder(s)
### mod_tile
COPY --from=builder /tmp/mod_tile /
COPY --chown=apache:apache --from=builder \
    /tmp/mod_tile_src/utils/example-map \
    /usr/share/renderd/example-map
COPY --from=builder \
    /tmp/mod_tile_src/etc/apache2/renderd-example-map.conf \
    /etc/httpd/conf.d/renderd-example-map.conf

## Add configuration
RUN printf '\n[example-map]\nMAXZOOM=20\nMINZOOM=0\nURI=/tiles/renderd-example\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf
RUN printf '\n[example-map-jpg]\nMAXZOOM=20\nMINZOOM=0\nTYPE=jpg image/jpeg jpeg\nURI=/tiles/renderd-example-jpg\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf
RUN printf '\n[example-map-png256]\nMAXZOOM=20\nMINZOOM=0\nTYPE=png image/png png256\nURI=/tiles/renderd-example-png256\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf
RUN printf '\n[example-map-png32]\nMAXZOOM=20\nMINZOOM=0\nTYPE=png image/png png32\nURI=/tiles/renderd-example-png32\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf
RUN printf '\n[example-map-webp]\nMAXZOOM=20\nMINZOOM=0\nTYPE=webp image/webp webp\nURI=/tiles/renderd-example-webp\nXML=/usr/share/renderd/example-map/mapnik.xml\n' >> /etc/renderd.conf

## Start services
CMD httpd -e debug -k start; \
    G_MESSAGES_DEBUG=${G_MESSAGES_DEBUG:-info} renderd --foreground
