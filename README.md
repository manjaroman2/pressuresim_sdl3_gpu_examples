## How to 

Reference: https://wiki.libsdl.org/SDL3/CategoryGPU  
https://github.com/TheSpydog/SDL_gpu_examples  
Firstly, you need to install SDL3 in your system, to be able to build the main file, which is linked with -lSDL3. 

To compile shaders from HLSL to SPIRV, you need to build [SDL_shadercross](https://github.com/libsdl-org/SDL_shadercross).   
Clone the repo and load the submodules, then cmake. If you're on Linux, it will probably complain about missing DirectXCompiler and dxil libs,   
which are required to translate from HLSL to SPRIV. To fix that just get the [dxc linux build](https://github.com/microsoft/DirectXShaderCompiler/releases)  
fortunately provided by Microsoft, and set the missing env vars in cmake.   
I recommend something like this:   
`$ cmake -S . -B build -DSDLSHADERCROSS_DXC=ON -DDirectXShaderCompiler_INCLUDE_PATH=external/linux_dxc/include -DDirectXShaderCompiler_dxcompiler_LIBRARY=external/linux_dxc/lib/libdxcompiler.so -DDirectXShaderCompiler_dxil_LIBRARY=external/linux_dxc/lib/libdxil.so`

You can now use `./compile-shaders.sh`, there is also 'libSDL3_shadercross', which can be linked, to compile shaders at runtime.   

Use `./compile-and-run.sh <example>` to study an example.  
e.g `./compile-and-run.sh basic-triangle`  

Custom dxc compilation:   
To compile with for example: -fvk-use-scalar-layout, shadercross does not support that, therefore we need to compile, ourselves:   
`linux_dxc/bin/dxc -T vs_6_0 -E main -spirv -fspv-target-env=vulkan1.0 -fvk-use-scalar-layout -O3 -Fo Line123.vert.spv shaders/source/Line.vert.hlsl`

### Todos

* fix textured quad example 
* container size should not stretch the paricles
* chunk grid shader


