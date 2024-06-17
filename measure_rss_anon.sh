#!/usr/bin/env bash

function runtest() {
    # Measure the startup memory usage (RssAnon) of process 'renderd' for 30 seconds,
    # then issue a render_list command and keep on monitoring for 4.5 more minutes.
    if [ -z "$1" ]; then
        echo "Supply argument to tag series"
        exit 1
    fi

    BUILD=build/$1
    mkdir -p $BUILD

    echo "Configuring..."
    cmake -B $BUILD -DMALLOC_LIB=$1 .

    echo "Building..."
    cmake --build $BUILD

    # Store the results in this file
    JSON=rssanon-$1.json5

    echo "Libraries:"
    ldd ./$BUILD/src/renderd | grep 'glib\|alloc'

    echo "Starting renderd..."
    ./$BUILD/src/renderd -f -c /etc/renderd.conf &

    SECONDS=0
    printf 'const %s = [\n' $1 >$JSON
    while [ $SECONDS -lt 30 ]
    do
        RSS=$(grep RssAnon /proc/$(pidof renderd)/status | awk '{print $2}')
        printf '{ "t": %d, "tag": "%s", "rss": %d },\n' $SECONDS $1 $RSS >>$JSON
        printf 'Time:%d Mem:%d\n' $SECONDS $RSS
        sleep 1
    done
    
    echo "Issuing render request..."
    ./$BUILD/src/render_list -c /etc/renderd.conf -n 8 --all --force --map=retina -z 6 -Z 6 &
    while [ $SECONDS -lt 300 ]
    do
        RSS=$(grep RssAnon /proc/$(pidof renderd)/status | awk '{print $2}')
        printf '{ "t": %d, "tag": "%s", "rss": %d },\n' $SECONDS $1 $RSS >>$JSON
        printf 'Time:%d Mem:%d\n' $SECONDS $RSS
        sleep 1
    done
    printf '{"t": %d, "tag": "%s", "rss": %d }];\n' $SECONDS $1 $RSS >>$JSON
    
    echo "Measurement completed. Results are here: $JSON. PID of renderd is: `pidof renderd`"
    ./$BUILD/src/render_list -c /etc/renderd.conf -S
    #kill $(pidof renderd)
}

function compare_malloc() {
    runtest glib
    sleep 10
    runtest jemalloc
    sleep 10
    runtest tcmalloc
    echo "Comparison completed."
}

# compare_malloc

runtest jemalloc
