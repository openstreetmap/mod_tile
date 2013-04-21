#!/bin/sh

set -e

BASE_DIR=/var/lib/mod_tile
WORKOSM_DIR=$BASE_DIR/.osmosis
LOCKFILE=/tmp/openstreetmap-update-expire-lock.txt
CHANGE_FILE=$BASE_DIR/changes.osc.gz
EXPIRY_FILE=$BASE_DIR/dirty_tiles

if [ $# -eq 1 ] ; then
    echo "Initialising Osmosis replication system to " $1
    mkdir $WORKOSM_DIR
    osmosis --read-replication-interval-init workingDirectory=$WORKOSM_DIR
    wget "http://toolserver.org/~mazder/replicate-sequences/?"$1"T00:00:00Z" -O $WORKOSM_DIR/state.txt
else
    echo "Updating tile server at " `date`

    if [ -e ${LOCKFILE} ] && kill -0 `cat ${LOCKFILE}`; then
        echo "already running"
        exit
    fi
    
# make sure the lockfile is removed when we exit and then claim it
    trap "rm -f ${LOCKFILE}; exit" INT TERM EXIT
    echo $$ > ${LOCKFILE}
    
    
    
    rm $EXPIRY_FILE || true
    touch $EXPIRY_FILE
    
    if [ ! -f ${CHANGE_FILE} ] ; then
        osmosis --read-replication-interval workingDirectory=$WORKOSM_DIR --simplify-change --write-xml-change $CHANGE_FILE
    fi
    osm2pgsql -a --slim -e 15:15 -o $EXPIRY_FILE $CHANGE_FILE
    render_expired --min-zoom=10 --max-zoom=18 --touch-from=10 -s /var/run/renderd.sock < $EXPIRY_FILE
    
    rm $CHANGE_FILE
    rm -f ${LOCKFILE}
    
fi