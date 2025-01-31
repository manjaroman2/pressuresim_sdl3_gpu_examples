#!/bin/bash

if [ -z "$1" ]; then
    set -- "basic_triangle" "$@"
fi
clear

mkdir -p build/
rm -f build/*

CC="clang"
# CC="gcc"
CFLAGS="-Werror -Wall -pedantic -Wno-gnu-statement-expression-from-macro-expansion" # -Wno-unused-variable
LINKFLAGS="-lSDL3 -g"
LINKS=""

if [ "$1" == "pressure-sim" ]; then
    $CC $CFLAGS -c pressure-sim-utils.c -o build/pressure-sim-utils.o
    LINKS="build/pressure-sim-utils.o"
fi

$CC $CFLAGS -c $1.c -o build/$1.o
$CC $CFLAGS $LINKFLAGS $LINKS build/$1.o -o build/$1.bin
./build/$1.bin

