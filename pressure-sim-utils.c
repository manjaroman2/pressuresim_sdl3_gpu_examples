#include "pressure-sim-utils.h"
#include <stdio.h>
#include <stdlib.h> 

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


float rand_float(float min, float max) {
    return min + (max - min) * (rand() / (float)RAND_MAX);
}


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


void vulkan_buffers_upload(SDL_GPUDevice* device, SDL_GPUBuffer* vertex_buffer, size_t vertex_size, uint32_t n_vertices, SDL_GPUBuffer* index_buffer, uint32_t n_indices, SDL_GPUTransferBuffer* transfer_buffer) {
    SDL_UnmapGPUTransferBuffer(device, transfer_buffer);
    SDL_GPUCommandBuffer* upload_cmdbuf = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(upload_cmdbuf);
    SDL_UploadToGPUBuffer(
        copy_pass,
        &(SDL_GPUTransferBufferLocation) {
            .transfer_buffer = transfer_buffer,
            .offset = 0
        },
        &(SDL_GPUBufferRegion) {
            .buffer = vertex_buffer,
            .offset = 0,
            .size = vertex_size * n_vertices 
        },
        false
    );
    SDL_UploadToGPUBuffer(
        copy_pass,
        &(SDL_GPUTransferBufferLocation) {
            .transfer_buffer = transfer_buffer,
            .offset = vertex_size * n_vertices
        },
        &(SDL_GPUBufferRegion) {
            .buffer = index_buffer,
            .offset = 0,
            .size = sizeof(uint16_t) * n_indices
        },
        false
    );
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(upload_cmdbuf);
    SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);
}


void vulkan_buffers_create(
    SDL_GPUDevice* device, 
    SDL_GPUBuffer** vertex_buffer_ptr,
    size_t vertex_size,
    uint32_t n_vertices,
    SDL_GPUBuffer** index_buffer_ptr,
    uint32_t n_indices,
    SDL_GPUTransferBuffer** transfer_buffer_ptr,
    void** transfer_data_ptr) {

    *vertex_buffer_ptr = SDL_CreateGPUBuffer( // this is safe!
        device, 
        &(SDL_GPUBufferCreateInfo) {
            .usage = SDL_GPU_BUFFERUSAGE_VERTEX, 
            .size = vertex_size * n_vertices
        }
    ); 

    *index_buffer_ptr = SDL_CreateGPUBuffer(
        device, 
        &(SDL_GPUBufferCreateInfo) {
            .usage = SDL_GPU_BUFFERUSAGE_INDEX, 
            .size = sizeof(uint16_t) * n_indices 
        }
    ); 

    *transfer_buffer_ptr = SDL_CreateGPUTransferBuffer( // this is safe!
        device,
        &(SDL_GPUTransferBufferCreateInfo) {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = (vertex_size * n_vertices) + (sizeof(uint16_t) * n_indices)
        }
    );

    *transfer_data_ptr = SDL_MapGPUTransferBuffer(device, *transfer_buffer_ptr, false);
}

