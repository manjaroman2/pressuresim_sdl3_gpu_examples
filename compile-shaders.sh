#!/bin/sh

clear
mkdir -p shaders/compiled
rm -f shaders/compiled/*

set -- \
    "Test123.vert" \
    "TexturedQuad.vert" \
    "RawTriangle.vert" \
    "PositionColor.vert" \
    "PositionColorInstanced.vert" \
    "PositionColorInstancedVertex.vert" \
    "Circle.vert" \
    "Line.vert" \
    "TexturedQuad.frag" \
    "SolidColor.frag" \
    "Circle.frag"
for shader in "$@"; do
    ./shadercross.bin "shaders/source/$shader.hlsl" -s HLSL -d SPIRV -o "shaders/compiled/$shader.spv" -Dfvk-use-scalar-layout=true
done
# tree "shaders/compiled"
