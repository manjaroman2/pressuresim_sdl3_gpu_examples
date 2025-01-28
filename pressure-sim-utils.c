#include "pressure-sim-utils.h"
#include <stdio.h>


const SDL_FColor COLOR_TRANSPARENT = { 0.0f, 0.0f, 0.0f, 0.0f }; 
const SDL_FColor COLOR_WHITE       = { 1.0f, 1.0f, 1.0f, 1.0f }; 
const SDL_FColor COLOR_BLACK       = { 0.0f, 0.0f, 0.0f, 1.0f }; 
const SDL_FColor COLOR_RED         = { 1.0f, 0.0f, 0.0f, 1.0f }; 
const SDL_FColor COLOR_GREEN       = { 0.0f, 1.0f, 0.0f, 1.0f }; 
const SDL_FColor COLOR_BLUE        = { 0.0f, 0.0f, 1.0f, 1.0f }; 
const SDL_FColor COLOR_CYAN        = { 0.0f, 1.0f, 1.0f, 1.0f }; 
const SDL_FColor COLOR_YELLOW      = { 1.0f, 1.0f, 0.0f, 1.0f }; 
const SDL_FColor COLOR_PINK        = { 1.0f, 0.0f, 1.0f, 1.0f }; 
const SDL_FColor COLOR_GRAY        = { RGBA_TO_FLOAT(36, 36, 36, 255) }; 


SDL_GPUShader* load_shader(
    SDL_GPUDevice* device, 
    const char* filename, 
    SDL_GPUShaderStage stage, 
    Uint32 sampler_count, 
    Uint32 uniform_buffer_count, 
    Uint32 storage_buffer_count, 
    Uint32 storage_texture_count) {

    if(!SDL_GetPathInfo(filename, NULL)) {
        fprintf(stdout, "File '%s' does not exist.\n", filename);
        return NULL;    
    }
        
    if (!(SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV)) {
        fprintf(stdout, "SDL_GPU_SHADERFORMAT_SPIRV not available.\n"); 
        return NULL; 
    }

    size_t code_size; 
    void* code = SDL_LoadFile(filename, &code_size); 
    if (code == NULL) {
        fprintf(stderr, "ERROR: SDL_LoadFile failed: %s\n", SDL_GetError());
        return NULL;  
    }

    SDL_GPUShaderCreateInfo shader_info = (SDL_GPUShaderCreateInfo) {
        .code = code,
        .code_size = code_size,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = stage,
        .num_samplers = sampler_count,
        .num_uniform_buffers = uniform_buffer_count,
        .num_storage_buffers = storage_buffer_count,
        .num_storage_textures = storage_texture_count
    };

    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &shader_info);

    if (shader == NULL) {
        fprintf(stderr, "ERROR: SDL_CreateGPUShader failed: %s\n", SDL_GetError());
        SDL_free(code); 
        return NULL;  
    }
    SDL_free(code); 
    return shader; 
} 
