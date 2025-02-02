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

source_directory="shaders/source"
compiled_directory="shaders/compiled"
echo "compiling from $source_directory -> $compiled_directory"
for file in "$source_directory"/*; do
    # Use a regular expression to extract the middle part (the second part of the filename)
	filename=$(basename "$file")
    if [[ "$filename" =~ ^([^\.]+)\.([^\.]+)\.([^\.]+)$ ]]; then
        middlepart="${BASH_REMATCH[2]}"
		filename="${BASH_REMATCH[1]}.${BASH_REMATCH[2]}"
        # Check if middlepart is "frag" or "vert"
        if [[ "$middlepart" == "frag" ]]; then
			echo "frag $filename.hlsl > $filename.spv"
			linux_dxc/bin/dxc -T ps_6_0 -E main -spirv -fspv-target-env=vulkan1.0 -fvk-use-scalar-layout -O3 -Fo "$compiled_directory/$filename.spv" "$source_directory/$filename.hlsl"
		elif [[ "$middlepart" == "vert" ]]; then
			echo "vert $filename.hlsl > $filename.spv"
			linux_dxc/bin/dxc -T vs_6_0 -E main -spirv -fspv-target-env=vulkan1.0 -fvk-use-scalar-layout -O3 -Fo "$compiled_directory/$filename.spv" "$source_directory/$filename.hlsl"
        fi
    fi
done

# for shader in "$@"; do
#     linux_dxc/bin/dxc -T vs_6_0 -E main -spirv -fspv-target-env=vulkan1.0 -fvk-use-scalar-layout -O3 -Fo "shaders/compiled/$1.spv" "shaders/source/$1.hlsl"
#     linux_dxc/bin/dxc -T ps_6_0 -E main -spirv -fspv-target-env=vulkan1.0 -fvk-use-scalar-layout -O3 -Fo "shaders/compiled/$1.spv" "shaders/source/$1.hlsl"
    # ./shadercross.bin "shaders/source/$shader.hlsl" -s HLSL -d SPIRV -o "shaders/compiled/$shader.spv" -Dfvk-use-scalar-layout=true
# done
# tree "shaders/compiled"
