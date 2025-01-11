#!/bin/bash
./shadercross.bin RawTriangle.vert.hlsl -s HLSL -d SPIRV -o RawTriangle.vert.spv
./shadercross.bin SolidColor.frag.hlsl -s HLSL -d SPIRV -o SolidColor.frag.spv
