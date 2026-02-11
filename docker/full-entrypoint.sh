#!/usr/bin/env sh

export DATADIR=/opt/data
export FONTDIR=/opt/fonts
export MAPNIK_XML=/opt/styles/mapnik.xml

sed -i \
    -e "s#^xml=.*#xml=${MAPNIK_XML}#Ig" \
    -e "s#^pid_file=#num_threads=-1\npid_file=#Ig" \
    -e "s#^font_dir=.*#font_dir=${FONTDIR}#Ig" \
    /etc/renderd.conf

if [ ! -f ${MAPNIK_XML} ]
then
    export OPENSTREETMAP_CARTO_DIR=/opt/openstreetmap-carto
    git clone https://github.com/openstreetmap-carto/openstreetmap-carto.git --depth 1 ${OPENSTREETMAP_CARTO_DIR}

    cp --archive ${OPENSTREETMAP_CARTO_DIR}/patterns ${OPENSTREETMAP_CARTO_DIR}/symbols /opt/styles/

    export EXTERNAL_DATA_SCRIPT_FLAGS="--cache --config ${OPENSTREETMAP_CARTO_DIR}/external-data.yml --data ${DATADIR} --no-update"
    export OSM2PGSQL_DATAFILE="${DATADIR}/region.osm.pbf"

    if [ ! -f ${OSM2PGSQL_DATAFILE} ]
    then
        curl --location "${DOWNLOAD_PBF:-http://download.geofabrik.de/asia/vietnam-latest.osm.pbf}" --output ${OSM2PGSQL_DATAFILE}
    fi

    cd ${OPENSTREETMAP_CARTO_DIR}

    sh scripts/docker-startup.sh import

    npm install --global carto
    carto ${OPENSTREETMAP_CARTO_DIR}/project.mml > ${MAPNIK_XML}
fi

apachectl -e debug -k start
renderd --foreground
