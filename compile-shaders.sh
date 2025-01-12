#!/bin/sh

mkdir -p shaders/compiled
rm -f shaders/compiled/*

set -- \
    "RawTriangle.vert" \
    "PositionColor.vert" \
    "PositionColorInstanced.vert" \
    "SolidColor.frag"
for shader in "$@"; do
    ./shadercross.bin "shaders/source/$shader.hlsl" -s HLSL -d SPIRV -o "shaders/compiled/$shader.spv"
done
tree "shaders/compiled"
