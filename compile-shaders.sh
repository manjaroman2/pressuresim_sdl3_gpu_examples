#!/bin/sh
source_directory="shaders/source"
compiled_directory="shaders/compiled"
vulkan_version="1.0"
mkdir -p $compiled_directory
rm -f $compiled_directory/*


echo "compiling from $source_directory -> $compiled_directory"
for file in "$source_directory"/*; do
	filename=$(basename "$file")
    if [[ "$filename" =~ ^([^\.]+)\.([^\.]+)\.([^\.]+)$ ]]; then
        middlepart="${BASH_REMATCH[2]}"
		filename="${BASH_REMATCH[1]}.${BASH_REMATCH[2]}"
		shader_language="${BASH_REMATCH[3]}"

        if [[ "$middlepart" == "frag" ]]; then
			echo "frag $filename.$shader_language > $filename.spv"
			if [[ "$shader_language" == "hlsl" ]]; then
			    linux_dxc/bin/dxc -T ps_6_0 -E main -spirv -fspv-target-env=vulkan$vulkan_version -fvk-use-scalar-layout -O3 -Fo "$compiled_directory/$filename.spv" "$source_directory/$filename.hlsl"
				# ./shadercross.bin "$source_directory/$filename.hlsl" -s HLSL -d SPIRV -o "$compiled_directory/$filename.spv"
			elif [[ "$shader_language" == "glsl" ]]; then
				# glslc -O --target-env=vulkan$vulkan_version -o "$compiled_directory/$filename.spv" "$source_directory/$filename.glsl"
				glslc -O --target-env=vulkan$vulkan_version "$source_directory/$filename.$shader_language" -fshader-stage=fragment -o "$compiled_directory/$filename.spv"
				# ./shadercross.bin "$source_directory/$filename.glsl" -s GLSL -d SPIRV -o "$compiled_directory/$filename.spv"
			fi
		elif [[ "$middlepart" == "vert" ]]; then
			echo "vert $filename.$shader_language > $filename.spv"
			if [[ "$shader_language" == "hlsl" ]]; then
				linux_dxc/bin/dxc -T vs_6_0 -E main -spirv -fspv-target-env=vulkan$vulkan_version -fvk-use-scalar-layout -O3 -Fo "$compiled_directory/$filename.spv" "$source_directory/$filename.hlsl"
				# ./shadercross.bin "$source_directory/$filename.hlsl" -s HLSL -d SPIRV -o "$compiled_directory/$filename.spv"
			elif [[ "$shader_language" == "glsl" ]]; then
				glslc -O -fshader-stage=vertex --target-env=vulkan$vulkan_version "$source_directory/$filename.$shader_language" -o "$compiled_directory/$filename.spv"
				# glslc -O --target-env=vulkan$vulkan_version -o "$compiled_directory/$filename.spv" "$source_directory/$filename.glsl"
				# ./shadercross.bin "$source_directory/$filename.glsl" -s GLSL -d SPIRV -o "$compiled_directory/$filename.spv"
			fi
        fi
    fi
done

# ./shadercross.bin "shaders/source/$shader.hlsl" -s HLSL -d SPIRV -o "shaders/compiled/$shader.spv" -Dfvk-use-scalar-layout=true
tree $compiled_directory
