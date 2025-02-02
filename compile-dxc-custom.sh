#!/bin/bash

linux_dxc/bin/dxc -T vs_6_0 -E main -spirv -fspv-target-env=vulkan1.0 -fvk-use-scalar-layout -O3 -Fo shaders/compiled/Line.vert.spv shaders/source/Line.vert.hlsl
