# Arguments
ARG centos_stream_version
ARG extra_repository
ARG mapnik_version=3.1.0

# Mapnik Builder
FROM quay.io/centos/centos:stream${centos_stream_version} as mapnik-builder

## Arguments
ARG centos_stream_version
ARG extra_repository
ARG mapnik_version

## Install mapnik-builder dependencies
RUN --mount=id=centos:stream${centos_stream_version}-/var/cache/dnf,target=/var/cache/dnf,type=cache,sharing=locked \
    echo "install_weak_deps=False" >> /etc/dnf/dnf.conf && \
    echo "keepcache=True" >> /etc/dnf/dnf.conf && \
    dnf --assumeyes install "dnf-command(config-manager)" && \
    dnf config-manager --save \
        --setopt=${extra_repository}.enabled=1 && \
    dnf --assumeyes install epel-release && \
    dnf --assumeyes upgrade && \
    dnf --assumeyes install \
        boost-devel \
        bzip2 \
        cairo-devel \
        freetype-devel \
        gcc \
        gcc-c++ \
        gdal-devel \
        harfbuzz-devel \
        libicu-devel \
        libjpeg-devel \
        libpng-devel \
        libtiff-devel \
        libwebp-devel \
        libxml2-devel \
        make \
        patch \
        postgresql-devel \
        proj-devel \
        python3 \
        sqlite-devel \
        tar \
        zlib-devel

## Download, Build & Install `Mapnik`
WORKDIR /tmp/mapnik_src
RUN --mount=id=centos:stream${centos_stream_version}-mapnik:${mapnik_version},target=/tmp/mapnik_src,type=cache \
    export DESTDIR="/tmp/mapnik"; \
    export GDAL_DATA="$(gdal-config --datadir)"; \
    export JOBS="$(nproc)"; \
    export PROJ_LIB="/usr/share/proj"; \
    export PYTHON="python3"; \
    mkdir --parents ${GDAL_DATA} ${PROJ_LIB}; \
    if [ ! -f SConstruct ]; then \
        curl --silent --location https://github.com/mapnik/mapnik/releases/download/v${mapnik_version}/mapnik-v${mapnik_version}.tar.bz2 \
            | tar --extract --bzip2 --strip-components=1 --file=-; \
        if [ "${centos_stream_version}" = "9" ]; then \
            curl --silent --location https://github.com/mapnik/mapnik/commit/8944e81367d2b3b91a41e24116e1813c01491e5d.patch \
                | patch -Np1; \
            curl --silent --location https://github.com/mapnik/mapnik/commit/83779b7b6bdd229740b1b5e12a4a8fe27114cb7d.patch \
                | patch -F3 -Np1; \
        fi \
    fi; \
    if [ "${centos_stream_version}" = "8" ]; then \
        export CUSTOM_DEFINES="-DACCEPT_USE_OF_DEPRECATED_PROJ_API_H=1"; \
    fi; \
    bash configure \
        CPP_TESTS=False \
        CUSTOM_CFLAGS="${CUSTOM_CFLAGS:-}" \
        CUSTOM_CXXFLAGS="${CUSTOM_CXXFLAGS:-}" \
        CUSTOM_DEFINES="${CUSTOM_DEFINES:-}" \
        CUSTOM_LDFLAGS="${CUSTOM_LDFLAGS:-}" \
        DEMO=False \
        DESTDIR="${DESTDIR}" \
        FAST=True \
        INPUT_PLUGINS=all \
        LIBDIR_SCHEMA=lib64 \
        OPTIMIZATION=2 \
        PREFIX=/usr \
        SVG2PNG=True \
        XMLPARSER=libxml2 && \
    make PYTHON="${PYTHON}" && \
    make install PYTHON="${PYTHON}"

# Builder
FROM quay.io/centos/centos:stream${centos_stream_version} as builder

## Arguments
ARG centos_stream_version
ARG extra_repository

## Install builder dependencies
RUN --mount=id=centos:stream${centos_stream_version}-/var/cache/dnf,target=/var/cache/dnf,type=cache,sharing=locked \
    echo "install_weak_deps=False" >> /etc/dnf/dnf.conf && \
    echo "keepcache=True" >> /etc/dnf/dnf.conf && \
    dnf --assumeyes install "dnf-command(config-manager)" && \
    dnf config-manager --save \
        --setopt=${extra_repository}.enabled=1 && \
    dnf --assumeyes install epel-release && \
    dnf --assumeyes upgrade && \
    dnf --assumeyes install \
        boost-devel \
        cairo-devel \
        cmake3 \
        gcc \
        gcc-c++ \
        gdal \
        glib2-devel \
        harfbuzz-devel \
        httpd-devel \
        iniparser-devel \
        libcurl-devel \
        libicu-devel \
        libjpeg \
        libmemcached-devel \
        librados2-devel \
        libtiff \
        libwebp \
        make \
        procps \
        proj

## Copy files from builder(s)
### Mapnik
COPY --from=mapnik-builder /tmp/mapnik /

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
FROM quay.io/centos/centos:stream${centos_stream_version} as runner

## Arguments
ARG centos_stream_version
ARG extra_repository

## Install runner dependencies
RUN --mount=id=centos:stream${centos_stream_version}-/var/cache/dnf,target=/var/cache/dnf,type=cache,sharing=locked \
    echo "install_weak_deps=False" >> /etc/dnf/dnf.conf && \
    echo "keepcache=True" >> /etc/dnf/dnf.conf && \
    dnf --assumeyes install "dnf-command(config-manager)" && \
    dnf config-manager --save \
        --setopt=${extra_repository}.enabled=1 && \
    dnf --assumeyes install epel-release && \
    dnf --assumeyes upgrade && \
    dnf --assumeyes install \
        boost-filesystem \
        boost-program-options \
        boost-regex \
        cairo \
        gdal \
        harfbuzz \
        httpd \
        iniparser \
        libicu \
        libmemcached \
        librados2 \
        libtiff \
        libwebp \
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
