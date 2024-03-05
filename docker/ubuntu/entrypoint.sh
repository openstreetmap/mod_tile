#!/usr/bin/env sh

if [ ! -f /data/style/mapnik.xml ]
then
    export DEBIAN_FRONTEND=noninteractive

    apt-get --yes update

    apt-get --no-install-recommends --yes install \
        curl \
        gdal-bin \
        git \
        node-carto \
        osm2pgsql \
        postgresql-client \
        python3-yaml \
        unzip

    git clone https://github.com/gravitystorm/openstreetmap-carto.git --depth 1 /data/style

    cd /data/style

    python3 scripts/get-external-data.py -c /data/style/external-data.yml -D /data/style/data

    scripts/get-fonts.sh

    psql --host "${PGHOST}" --user "${PGUSER}" --dbname "${PGDATABASE}" --command "CREATE EXTENSION postgis;"
    psql --host "${PGHOST}" --user "${PGUSER}" --dbname "${PGDATABASE}" --command "CREATE EXTENSION hstore;"
    psql --host "${PGHOST}" --user "${PGUSER}" --dbname "${PGDATABASE}" --command "ALTER TABLE geometry_columns OWNER TO ${PGUSER};"
    psql --host "${PGHOST}" --user "${PGUSER}" --dbname "${PGDATABASE}" --command "ALTER TABLE spatial_ref_sys OWNER TO ${PGUSER};"

    curl --location "${DOWNLOAD_PBF:-http://download.geofabrik.de/asia/vietnam-latest.osm.pbf}" --output /data/region.osm.pbf

    osm2pgsql \
        --create \
        --database "${PGDATABASE}" \
        --host "${PGHOST}" \
        --hstore \
        --number-processes "$(nproc)" \
        --slim \
        --tag-transform-script /data/style/openstreetmap-carto.lua \
        --user "${PGUSER}" \
        -G \
        -S /data/style/openstreetmap-carto.style \
        /data/region.osm.pbf

    psql --host "${PGHOST}" --user "${PGUSER}" --dbname "${PGDATABASE}" --file /data/style/indexes.sql

    carto /data/style/project.mml > /data/style/mapnik.xml
    sed \
        -e 's#/usr/share/renderd/example-map/mapnik.xml#/data/style/mapnik.xml#g' \
        -e 's/URI=/MAXZOOM=20\nMINZOOM=0\nURI=/g' \
        -e 's#font_dir=/usr/share/fonts#font_dir=/data/style/fonts#g' \
        /etc/renderd.conf > /data/renderd.conf
fi

sed -i 's#/etc/renderd.conf#/data/renderd.conf#g' /etc/apache2/sites-enabled/renderd-example-map.conf
sed -i 's/maxZoom: 12/maxZoom: 20/g' /usr/share/renderd/example-map/index.html

apachectl -e debug -k start
renderd --config /data/renderd.conf --foreground
