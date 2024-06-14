#!/usr/bin/env bash

function runtest() {
    # Measure the startup memory usage (RssAnon) of process 'renderd' for 30 seconds,
    # then issue a render_list command and keep on monitoring for 4.5 more minutes.
    if [ -z "$1" ]; then
	echo "Supply argument to tag series"
	exit 1
    fi

    echo "Configuring..."
    cmake -B build -DMALLOC_LIB=$1 .

    echo "Building..."
    cmake --build build
    
    # Store the results in this file
    JSON=rssanon-$1.json5

    echo "Libraries:"
    ldd ./build/src/renderd | grep 'glib\|alloc'
    
    echo "Starting renderd..."
    ./build/src/renderd -f -c /etc/renderd.conf &
    
    SECONDS=0
    printf '[\n' >$JSON
    while [ $SECONDS -lt 30 ]
    do
	RSS=$(grep RssAnon /proc/$(pidof renderd)/status | awk '{print $2}')
	printf '{ "t": %d, "tag": "%s", "rss": %d },\n' $SECONDS $1 $RSS >>$JSON
	printf 'Time:%d Mem:%d\n' $SECONDS $RSS
	sleep 1
    done
    
    echo "Issuing render request..."
    ./build/src/render_list -c /etc/renderd.conf -n 8 --all --force --map=retina -z 6 -Z 6 &
    while [ $SECONDS -lt 300 ]
    do
	RSS=$(grep RssAnon /proc/$(pidof renderd)/status | awk '{print $2}')
	printf '{ "t": %d, "tag": "%s", "rss": %d },\n' $SECONDS $1 $RSS >>$JSON
	printf 'Time:%d Mem:%d\n' $SECONDS $RSS
	sleep 1
    done
    printf ']\n' >>$JSON
    
    echo "Measurement completed. Results are here: $JSON"
    kill $(pidof renderd)
}

runtest glib
sleep 10
runtest jemalloc
sleep 10
runtest tcmalloc

echo "All done."
