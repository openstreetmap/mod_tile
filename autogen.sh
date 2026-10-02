#!/bin/sh

set -- -vfi

if autoreconf --help 2>&1 | grep -q -- '--replace-handwritten'
then
    set -- "$@" --replace-handwritten
fi

exec autoreconf "$@"
