FROM ubuntu:focal
EXPOSE 80
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update --yes \
 && apt-get install --yes \
    apache2 apache2-dev \
    build-essential curl libcairo2-dev \
    libcurl4-gnutls-dev libglib2.0-dev \
    libiniparser-dev libmapnik-dev \
 && apt-get clean \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /usr/src/mod_tile

COPY . ${PWD}

RUN ./autogen.sh \
 && ./configure --bindir=/usr/bin \
 && make \
 && make install \
 && make install-mod_tile

RUN ln -s "${PWD}/examples/config/renderd/renderd.conf.dist" /etc/renderd.conf \
 && mkdir -p /run/renderd /var/cache/renderd/tiles

RUN mkdir -p examples/example-map/leaflet \
 && curl "https://unpkg.com/leaflet@1.7.1/dist/leaflet.js" > examples/example-map/leaflet/leaflet.min.js \
 && curl "https://unpkg.com/leaflet@1.7.1/dist/leaflet.css" > examples/example-map/leaflet/leaflet.css \
 && ln -s "${PWD}/examples/example-map" /var/www/ \
 && ln -s "${PWD}/examples/config/apache2/renderd.conf.dist" /etc/apache2/conf-enabled/renderd.conf \
 && ln -s "${PWD}/examples/config/apache2/renderd-example-map.conf.dist" /etc/apache2/conf-enabled/renderd-example-map.conf \
 && echo "LoadModule tile_module /usr/lib/apache2/modules/mod_tile.so" > /etc/apache2/mods-enabled/mod_tile.load

RUN printf "#!/bin/bash\nrenderd\nexec apache2ctl -D FOREGROUND" > /docker-entrypoint.sh \
 && chmod +x /docker-entrypoint.sh

ENTRYPOINT ["/docker-entrypoint.sh"]
