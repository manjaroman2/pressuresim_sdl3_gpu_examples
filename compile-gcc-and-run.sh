#!/bin/bash

if [ -z "$1" ]; then
    set -- "basic_triangle" "$@"
fi
clear

mkdir -p build/
rm -f build/*

CFLAGS="-lSDL3 -Werror -Wall -Wno-unused-variable -pedantic"
LINKS=""

if [ "$1" == "pressure-sim" ]; then
    gcc $CFLAGS -c pressure-sim-utils.c -o build/pressure-sim-utils.o
    LINKS="build/pressure-sim-utils.o"
fi

gcc $CFLAGS -c $1.c -o build/$1.o
gcc $CFLAGS $LINKS build/$1.o -o build/$1.bin
./build/$1.bin

