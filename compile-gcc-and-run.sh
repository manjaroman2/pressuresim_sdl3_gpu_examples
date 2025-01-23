#!/bin/bash

if [ -z "$1" ]; then
    set -- "basic_triangle" "$@"
fi
clear
gcc -lSDL3 -Werror -Wall -Wno-unused-variable -pedantic -o $1.bin $1.c && ./$1.bin
