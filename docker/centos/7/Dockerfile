# Arguments
ARG mapnik_version=3.1.0

# Mapnik Builder
FROM centos:centos7 as mapnik-builder

## Arguments
ARG mapnik_version

## Install mapnik-builder dependencies
RUN --mount=id=centos:centos7-/var/cache/yum,target=/var/cache/yum,type=cache,sharing=locked \
    ulimit -n 40000; \
    echo "install_weak_deps=0" >> /etc/yum.conf && \
    sed --in-place 's/keepcache=0/keepcache=1/g' /etc/yum.conf && \
    yum --assumeyes install epel-release centos-release-scl && \
    yum --assumeyes upgrade && \
    yum --assumeyes install \
        boost169-devel \
        bzip2 \
        cairo-devel \
        devtoolset-9-gcc \
        devtoolset-9-gcc-c++ \
        freetype-devel \
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
RUN --mount=id=centos:centos7-mapnik:${mapnik_version},target=/tmp/mapnik_src,type=cache \
    export DESTDIR="/tmp/mapnik"; \
    export GDAL_DATA="$(gdal-config --datadir)"; \
    export JOBS="$(nproc)"; \
    export PROJ_LIB="/usr/share/proj"; \
    export PYTHON="python3"; \
    mkdir --parents ${GDAL_DATA} ${PROJ_LIB}; \
    if [ ! -f SConstruct ]; then \
        curl --silent --location https://github.com/mapnik/mapnik/releases/download/v${mapnik_version}/mapnik-v${mapnik_version}.tar.bz2 \
            | tar --extract --bzip2 --strip-components=1 --file=-; \
        sed -i 's#BOOST_APPEND = match.groups()\[0\]#BOOST_APPEND = ""#g' SConstruct; \
    fi; \
    source /opt/rh/devtoolset-9/enable; \
    export CUSTOM_DEFINES="-DACCEPT_USE_OF_DEPRECATED_PROJ_API_H=1"; \
    export CUSTOM_LDFLAGS="-L/usr/lib64/boost169"; \
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
FROM centos:centos7 as builder

## Install builder dependencies
RUN --mount=id=centos:centos7-/var/cache/yum,target=/var/cache/yum,type=cache,sharing=locked \
    ulimit -n 40000; \
    echo "install_weak_deps=0" >> /etc/yum.conf && \
    sed --in-place 's/keepcache=0/keepcache=1/g' /etc/yum.conf && \
    yum --assumeyes install epel-release centos-release-scl && \
    yum --assumeyes upgrade && \
    yum --assumeyes install \
        boost169-devel \
        cairo-devel \
        cmake3 \
        devtoolset-9-gcc \
        devtoolset-9-gcc-c++ \
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
        proj

## Copy files from builder(s)
### Mapnik
COPY --from=mapnik-builder /tmp/mapnik /

## Build, Test & Install `mod_tile`
COPY . /tmp/mod_tile_src
WORKDIR /tmp/mod_tile_build
RUN source /opt/rh/devtoolset-9/enable && \
    export CMAKE_BUILD_PARALLEL_LEVEL=$(nproc) && \
    export CFLAGS="-I/usr/include/boost169" && \
    export CXXFLAGS="-I/usr/include/boost169" && \
    cmake3 -B . -S /tmp/mod_tile_src \
        -DCMAKE_BUILD_TYPE:STRING=Release \
        -DCMAKE_INSTALL_LOCALSTATEDIR=/var \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_INSTALL_RUNSTATEDIR=/run \
        -DCMAKE_INSTALL_SYSCONFDIR=/etc \
        -DENABLE_TESTS:BOOL=ON && \
    cmake3 --build .
RUN export CTEST_PARALLEL_LEVEL=$(nproc) && \
    export DESTDIR=/tmp/mod_tile && \
    ctest3 --output-on-failure && \
    (cmake3 --install . --strip || make DESTDIR=${DESTDIR} install/strip)

# Runner
FROM centos:centos7 as runner

## Install runner dependencies
RUN --mount=id=centos:centos7-/var/cache/yum,target=/var/cache/yum,type=cache,sharing=locked \
    ulimit -n 40000; \
    echo "install_weak_deps=0" >> /etc/yum.conf && \
    sed --in-place 's/keepcache=0/keepcache=1/g' /etc/yum.conf && \
    yum --assumeyes install epel-release && \
    yum --assumeyes upgrade && \
    yum --assumeyes install \
        boost169-filesystem \
        boost169-program-options \
        boost169-regex \
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
