#!/bin/bash
if [ -z "$1" ]; then
    set -- "basic_triangle" "$@"
fi
clear

./compile.sh $@
# perf record -e cache-references,cache-misses,cycles,instructions,branches,faults,migrations ./build/$1.bin
./build/$1.bin

