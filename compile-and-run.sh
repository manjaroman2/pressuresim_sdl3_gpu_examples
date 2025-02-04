#!/bin/bash

if [ -z "$1" ]; then
    set -- "basic_triangle" "$@"
fi
clear

mkdir -p build/
rm -f build/*

CC="clang"
# CC="gcc"
CFLAGS_RELEASE="-Oz -Werror -Wall -pedantic -Wno-gnu-statement-expression-from-macro-expansion" #
LINKFLAGS_RELEASE="-lSDL3 -g -s"
CFLAGS_DEBUG="-ggdb -O2 -Werror -Wall -pedantic -Wno-gnu-statement-expression-from-macro-expansion -Wno-unused-variable" #
LINKFLAGS_DEBUG="-I/usr/local/include -L/usr/local/lib -lSDL3 -lvulkan -g"
# export LD_LIBRARY_PATH=~/src/SDL/build:$LD_LIBRARY_PATH
# export C_INCLUDE_PATH=~/src/SDL/include:$C_INCLUDE_PATH
# export PKG_CONFIG_PATH=~/src/SDL/build:$PKG_CONFIG_PATH
pkg-config --modversion sdl3
CFLAGS=$CFLAGS_DEBUG
LINKFLAGS=$LINKFLAGS_DEBUG
# CFLAGS=$CFLAGS_RELEASE
# LINKFLAGS=$LINKFLAGS_RELEASE
LINKS=""

if [ "$1" == "pressure-sim" ]; then
    $CC $CFLAGS -c pressure-sim-utils.c -o build/pressure-sim-utils.o
    LINKS="build/pressure-sim-utils.o"
fi

$CC $CFLAGS -c $1.c -o build/$1.o
$CC $CFLAGS $LINKFLAGS $LINKS build/$1.o -o build/$1.bin
ls -l ./build/$1.bin
file ./build/$1.bin
./build/$1.bin

