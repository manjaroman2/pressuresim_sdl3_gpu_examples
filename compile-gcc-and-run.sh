#!/bin/bash

if [ -z "$1" ]; then
    set -- "basic_triangle" "$@"
fi
gcc -lSDL3 -Werror -o $1.bin $1.c && ./$1.bin
