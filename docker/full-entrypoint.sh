#!/usr/bin/env sh

if [ ! -f /opt/styles/mapnik.xml ]
then
    git clone https://github.com/gravitystorm/openstreetmap-carto.git --depth 1 /opt/openstreetmap-carto

    cp --archive /opt/openstreetmap-carto/patterns /opt/openstreetmap-carto/symbols /opt/styles/

    python3 /opt/openstreetmap-carto/scripts/get-external-data.py --cache --config /opt/openstreetmap-carto/external-data.yml --data /opt/data

    cd /opt && /opt/openstreetmap-carto/scripts/get-fonts.sh && cd -

    psql --command "CREATE EXTENSION postgis;" --dbname "${PGDATABASE}" --host "${PGHOST}" --user "${PGUSER}"
    psql --command "CREATE EXTENSION hstore;" --dbname "${PGDATABASE}" --host "${PGHOST}" --user "${PGUSER}"
    psql --command "ALTER TABLE geometry_columns OWNER TO ${PGUSER};" --dbname "${PGDATABASE}" --host "${PGHOST}" --user "${PGUSER}"
    psql --command "ALTER TABLE spatial_ref_sys OWNER TO ${PGUSER};" --dbname "${PGDATABASE}" --host "${PGHOST}" --user "${PGUSER}"

    if [ ! -f /opt/data/region.osm.pbf ]
    then
        curl --location "${DOWNLOAD_PBF:-http://download.geofabrik.de/asia/vietnam-latest.osm.pbf}" --output /opt/data/region.osm.pbf
    fi

    osm2pgsql \
        --create \
        --database "${PGDATABASE}" \
        --host "${PGHOST}" \
        --hstore \
        --number-processes "$(nproc)" \
        --slim \
        --tag-transform-script /opt/openstreetmap-carto/openstreetmap-carto.lua \
        --user "${PGUSER}" \
        -G \
        -S /opt/openstreetmap-carto/openstreetmap-carto.style \
        /opt/data/region.osm.pbf

    psql --dbname "${PGDATABASE}" --file /opt/openstreetmap-carto/indexes.sql --host "${PGHOST}" --user "${PGUSER}"

    npm install --global carto
    carto /opt/openstreetmap-carto/project.mml > /opt/styles/mapnik.xml

    chmod --recursive 777 /opt/*
fi

sed -i \
    -e 's#/usr/share/renderd/example-map/mapnik.xml#/opt/styles/mapnik.xml#g' \
    -e 's/pid_file=/num_threads=-1\npid_file=/g' \
    -e 's#font_dir=.*#font_dir=/opt/fonts#g' \
    /etc/renderd.conf

apachectl -e debug -k start
renderd --foreground
