#!/bin/sh

# Copyright © 2013 mod_tile contributors
#
# This file is part of mod_tile.
#
# mod_tile is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 2 of the License, or (at your
# option) any later version.
#
# mod_tile is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with mod_tile.  If not, see <http://www.gnu.org/licenses/>.

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
