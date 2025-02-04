#include "pressure-sim-utils.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <stdint.h>
#include <stdlib.h> 
#include <stdio.h> 

#define vec2_unpack(_vec) ((_vec).x), ((_vec).y)


#define box_overlap(_b1, _b2) ((_b1).r >= (_b2).l && (_b1).l <= (_b2).r && (_b1).t >= (_b2).b && (_b1).b <= (_b2).t) 

#define N 50000
#define R 2.0f 
#define WINDOW_WIDTH  1200
#define WINDOW_HEIGHT 1000 


// https://github.com/microsoft/DirectXShaderCompiler/wiki/Buffer-Packing
// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules
// https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst#constant-texture-structured-byte-buffers
typedef struct {
    float x, y;    // 8 bytes  
    uint32_t flags; 
    uint32_t _pad; 
} GPULine; 


typedef struct {
    float x, y; // gpu coords 8 bytes 
} GPUParticle; 


typedef struct {
    float l, r, b, t; 
} Box; 


typedef struct {
    uint32_t x, y; 
} Vec2i; 


typedef struct {
    float x; 
    float y; 
} Vec2f; 


typedef struct {
    float x, y, z; 
} Vec3f; 


typedef struct {
    Vec2i n; 
    Vec2f size; 
} ChunkmapInfo; 


typedef struct Particle Particle; 


typedef struct Chunk {
    Particle** particles; 
    struct Chunk* left; 
    struct Chunk* right; 
    struct Chunk* bottom; 
    struct Chunk* top; 
    Box box;
    uint32_t n_filled; 
    uint32_t n_free; 
    Vec2i xy; 
} Chunk; 


typedef struct {
    Chunk* chunk;
    uint32_t p_index; // particle index in chunk 
} ChunkRef; 


typedef enum { 
    INVALID, 
    ONE,
    TOP_BOTTOM,
    LEFT_RIGHT,
    LRTB
} ChunkState;


struct Particle {
    ChunkRef chunk_ref[4]; 
    ChunkState chunk_state; 
    GPUParticle gpu;
    Box box; 
    Vec2f p; 
    Vec2f v; 
    float m; 
}; 


typedef struct {
    uint32_t width, height; 
    float zoom, inverse_aspect_ratio, scalar; 
} Container; 


typedef struct {
    SDL_GPUGraphicsPipeline* pipeline; 
    SDL_GPUBuffer* vertex_buffer;
    SDL_GPUBuffer* index_buffer;
    SDL_GPUBuffer* sso_buffer; 
    SDL_GPUTransferBuffer* sso_transfer_buffer;
} Destroyer;


#define particle_collisions(_p, _chunk_ref) ({                                               \
    do {                                                                                     \
        for (uint32_t j = 0; j < (_chunk_ref)->p_index; j++) {                               \
            collide((_p), (_chunk_ref)->chunk->particles[j]);                                \
        }                                                                                    \
        for (uint32_t j = (_chunk_ref)->p_index+1; j < (_chunk_ref)->chunk->n_filled; j++) { \
            collide((_p), (_chunk_ref)->chunk->particles[j]);                                \
        }                                                                                    \
    } while(0);                                                                              \
})

#define particle_update_chunk_ref(_p, _i, _chunk, _p_index) ({ \
    do {                                                       \
        (_p)->chunk_ref[(_i)].chunk = (_chunk);                \
        (_p)->chunk_ref[(_i)].p_index = (_p_index);            \
    } while(0);                                                \
}) 

#define chunk_append(_chunk, _p) ({                                            \
    uint32_t _p_index = UINT32_MAX;                                            \
    do {                                                                       \
        if ((_chunk)->n_free == 0) {                                           \
            fprintf(stderr, "ERROR: Cannot append particle to full chunk.\n"); \
            break;                                                             \
        }                                                                      \
        _p_index = (_chunk)->n_filled;                                         \
        (_chunk)->particles[_p_index] = (_p);                                  \
        (_chunk)->n_filled++;                                                  \
        (_chunk)->n_free--;                                                    \
    } while(0);                                                                \
    _p_index;                                                                  \
})

#define chunk_pop(_chunk_ref) ({                                                                                               \
    do {                                                                                                                       \
        if ((_chunk_ref)->chunk->n_filled == 0) {                                                                              \
            fprintf(stderr, "ERROR: popping from empty chunk.\n");                                                             \
            abort();                                                                                                           \
        }                                                                                                                      \
        (_chunk_ref)->chunk->n_filled--;                                                                                       \
        (_chunk_ref)->chunk->particles[(_chunk_ref)->p_index] = (_chunk_ref)->chunk->particles[(_chunk_ref)->chunk->n_filled]; \
        (_chunk_ref)->chunk->n_free++;                                                                                         \
        (_chunk_ref)->chunk->particles[(_chunk_ref)->chunk->n_filled] = NULL;                                                  \
    } while(0);                                                                                                                \
})

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


void print_vk_info(void) {
    /* VkInstance vk_instance; */ 
    /* VkApplicationInfo vk_app_info = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "SDL Vulkan App"  }; */ 
    /* VkInstanceCreateInfo vk_create_info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &vk_app_info }; */ 
    /* if (vkCreateInstance(&vk_create_info, NULL, &vk_instance) != VK_SUCCESS) { */
    /*     fprintf(stderr, "ERROR: \n"); */
    /*     return 1; */ 
    /* } */
    /* uint32_t gpuCount = 0; */
    /* vkEnumeratePhysicalDevices(vk_instance, &gpuCount, NULL); */
    /* VkPhysicalDevice physicalDevices[gpuCount]; */
    /* vkEnumeratePhysicalDevices(vk_instance, &gpuCount, physicalDevices); */

    /* VkPhysicalDevice physicalDevice = physicalDevices[0]; // Use the first GPU (or select based on criteria) */
    /* VkPhysicalDeviceProperties deviceProperties; */
    /* vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties); */

    /* VkDeviceSize minAlignment = deviceProperties.limits.minStorageBufferOffsetAlignment; */
    /* printf("Minimum Storage Buffer Offset Alignment: %llu bytes\n", (unsigned long long)minAlignment); */
    /* return 1; */ 
}


void destroy_sdl(
    SDL_GPUDevice* device, 
    SDL_Window* window,
    Destroyer* destroyers,
    uint32_t n_destroyers,
    SDL_GPUGraphicsPipeline* pipeline1,
    SDL_GPUTexture* texture) {
    for (uint32_t i = 0; i < n_destroyers; i++) {
        SDL_ReleaseGPUGraphicsPipeline(device, destroyers[i].pipeline); 
        SDL_ReleaseGPUBuffer(device, destroyers[i].vertex_buffer); 
        SDL_ReleaseGPUBuffer(device, destroyers[i].index_buffer); 
        SDL_ReleaseGPUBuffer(device, destroyers[i].sso_buffer); 
        SDL_ReleaseGPUTransferBuffer(device, destroyers[i].sso_transfer_buffer); 
    }
    SDL_ReleaseGPUGraphicsPipeline(device, pipeline1);
    SDL_ReleaseGPUTexture(device, texture);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyWindow(window);
    SDL_DestroyGPUDevice(device);
    SDL_Quit();
}


void handle_event(SDL_Event event, bool* quit, bool* debug_mode, uint* sim_state) {
    switch (event.type) {
        case SDL_EVENT_QUIT: {
            *quit = true; 
        } break; 
        case SDL_EVENT_KEY_DOWN: {
            switch (event.key.key) {
                case SDLK_Q: {
                    *quit = true; 
                } break;  
                case SDLK_W: {

                } break; 
                case SDLK_S: {

                } break; 
                case SDLK_D: {
                    *debug_mode = !*debug_mode; 
                } break; 
                case SDLK_SPACE: {
                    if (*sim_state == 0)
                        *sim_state = 1; 
                    else if (*sim_state == 1)
                        *sim_state = 0; 
                } break;
            }
        } break; 
    } 
}


float rand_float(float min, float max) {
    return min + (max - min) * (rand() / (float)RAND_MAX);
}


void collide(Particle* p1, Particle* p2) {
    if (p1 == NULL) {
        return; 
    } 
    if (p2 == NULL) {
        return; 
    } 
    /* float v_tmp; */ 
    /* v_tmp = p1->v.x; */ 
    /* p1->v.x = p2->v.x; */ 
    /* p2->v.x = v_tmp; */ 
    /* v_tmp = p1->v.y; */ 
    /* p1->v.y = p2->v.y; */ 
    /* p2->v.y = v_tmp; */ 
}


int physics_tick(
    float dt, 
    Particle* particles, 
    uint32_t n_particles, 
    float p_radius, 
    Container* container, 
    Chunk*** chunkmap, 
    ChunkmapInfo chunkmap_info, 
    Chunk** border_chunks, 
    uint32_t n_border_chunks) {

    for (uint32_t i = 0; i < n_particles; i++) {
        Particle* p = &particles[i]; 
        // p->v.y -= 1.0f; 
        p->v.x += rand_float(-10.0f, 100.0f);  
        /* p->v.y += rand_float(-100.0f, 10.0f); */
        float dx = p->v.x*dt; 
        float dy = p->v.y*dt; 
        p->p.x += dx;  
        p->p.y += dy;  

        p->box.l += dx;  
        p->box.r += dx;  
        p->box.b += dy; 
        p->box.t += dy; 

        p->gpu.x += dx*container->scalar;
        p->gpu.y += dy*container->zoom;

        switch (p->chunk_state) {
            case ONE: {
                ChunkRef chunk_ref = p->chunk_ref[0]; 
                particle_collisions(p, &chunk_ref);
                if (p->box.b < chunk_ref.chunk->box.b) { // bottom edge crossed bottom 
                    if (chunk_ref.chunk->bottom != NULL) {
                        uint32_t p_index_bottom = chunk_append(chunk_ref.chunk->bottom, p);
                        if (p_index_bottom == UINT32_MAX) {
                            return -1; 
                        }
                        if (p->box.t < chunk_ref.chunk->box.b) { // top edge crossed bottom 
                            chunk_pop(&chunk_ref); 
                            particle_update_chunk_ref(p, 0, chunk_ref.chunk->bottom, p_index_bottom); 
                        } else {
                            p->chunk_state = TOP_BOTTOM;
                            particle_update_chunk_ref(p, 2, chunk_ref.chunk, chunk_ref.p_index); 
                            particle_update_chunk_ref(p, 3, chunk_ref.chunk->bottom, p_index_bottom); 
                        }
                    } else {
                        p->v.y *= -1; 
                    }
                } else if (p->box.t > chunk_ref.chunk->box.t) { // top edge crossed top 
                    if (chunk_ref.chunk->top != NULL) {
                        uint32_t p_index_top = chunk_append(chunk_ref.chunk->top, p);
                        if (p_index_top == UINT32_MAX) {
                            return -1; 
                        }
                        if (p->box.b > chunk_ref.chunk->box.t) { // bottom edge crossed top
                            chunk_pop(&chunk_ref); 
                            particle_update_chunk_ref(p, 0, chunk_ref.chunk->top, p_index_top); 
                        } else {
                            p->chunk_state = TOP_BOTTOM; 
                            particle_update_chunk_ref(p, 2, chunk_ref.chunk->top, p_index_top); 
                            particle_update_chunk_ref(p, 3, chunk_ref.chunk, chunk_ref.p_index); 
                        }
                    } else {
                        p->v.y *= -1; 
                    }
                }
                if (p->box.l < chunk_ref.chunk->box.l) { // left edge crossed left 
                    if (chunk_ref.chunk->left != NULL) {
                        uint32_t p_index_left = chunk_append(chunk_ref.chunk->left, p);
                        if (p_index_left == UINT32_MAX) {
                            return -1;
                        } 
                        if (p->box.r < chunk_ref.chunk->box.l) { // right edge crossed left 
                            chunk_pop(&chunk_ref); 
                            particle_update_chunk_ref(p, 0, chunk_ref.chunk->left, p_index_left); 
                        } else {
                            p->chunk_state = LEFT_RIGHT; 
                            particle_update_chunk_ref(p, 0, chunk_ref.chunk->left, p_index_left); 
                            particle_update_chunk_ref(p, 1, chunk_ref.chunk, chunk_ref.p_index); 
                        }
                    } else {
                        p->v.x *= -1; 
                    }
                } else if (p->box.r > chunk_ref.chunk->box.r) { // right edge crossed right 
                    if (chunk_ref.chunk->right != NULL) {
                        uint32_t p_index_right = chunk_append(chunk_ref.chunk->right, p);
                        if (p_index_right == UINT32_MAX) {
                            return -1; 
                        }
                        if (p->box.l > chunk_ref.chunk->box.r) { // left edge crossed right
                            chunk_pop(&chunk_ref); 
                            particle_update_chunk_ref(p, 0, chunk_ref.chunk->right, p_index_right); 
                        } else {
                            p->chunk_state = LEFT_RIGHT;
                            particle_update_chunk_ref(p, 0, chunk_ref.chunk, chunk_ref.p_index); 
                            particle_update_chunk_ref(p, 1, chunk_ref.chunk->right, p_index_right); 
                        }
                    } else {
                        p->v.x *= -1; 
                    }
                }
            } break;
            case TOP_BOTTOM: { 
                ChunkRef chunk_ref_top = p->chunk_ref[2]; 
                ChunkRef chunk_ref_bottom = p->chunk_ref[3]; 
                particle_collisions(p, &chunk_ref_top);
                particle_collisions(p, &chunk_ref_bottom);
                if (p->box.r > chunk_ref_bottom.chunk->box.r) { // right edge crossed right
                    if (chunk_ref_bottom.chunk->right != NULL) {
                        Chunk* chunk_bottom_right = chunk_ref_bottom.chunk->right; 
                        Chunk* chunk_top_right = chunk_ref_top.chunk->right; 
                        uint32_t p_index_bottom_right = chunk_append(chunk_bottom_right, p); 
                        uint32_t p_index_top_right = chunk_append(chunk_top_right, p); 
                        if (p_index_bottom_right == UINT32_MAX || p_index_top_right == UINT32_MAX) {
                            return -1; 
                        }
                        if (p->box.l > chunk_ref_bottom.chunk->box.r) { // left edge crossed right 
                            chunk_pop(&chunk_ref_top); 
                            chunk_pop(&chunk_ref_bottom); 
                            particle_update_chunk_ref(p, 2, chunk_top_right, p_index_top_right); 
                            particle_update_chunk_ref(p, 3, chunk_bottom_right, p_index_bottom_right); 
                        } else {
                            p->chunk_state = LRTB; 
                            Chunk* chunk_top_left = chunk_ref_top.chunk; 
                            Chunk* chunk_bottom_left = chunk_ref_bottom.chunk; 
                            uint32_t p_index_top_left = chunk_ref_top.p_index;
                            uint32_t p_index_bottom_left = chunk_ref_bottom.p_index;
                            particle_update_chunk_ref(p, 0, chunk_bottom_right, p_index_bottom_right); 
                            particle_update_chunk_ref(p, 1, chunk_top_right, p_index_top_right); 
                            particle_update_chunk_ref(p, 2, chunk_top_left, p_index_top_left); 
                            particle_update_chunk_ref(p, 3, chunk_bottom_left, p_index_bottom_left); 
                        }
                    } else {
                        p->v.x *= -1; 
                    }
                } else if (p->box.l < chunk_ref_bottom.chunk->box.l) { // left edge crossed left 
                    if (chunk_ref_bottom.chunk->left != NULL) {
                        Chunk* chunk_top_left = chunk_ref_top.chunk->left;
                        Chunk* chunk_bottom_left = chunk_ref_bottom.chunk->left;
                        uint32_t p_index_bottom_left = chunk_append(chunk_ref_bottom.chunk->left, p); 
                        uint32_t p_index_top_left = chunk_append(chunk_ref_top.chunk->left, p); 
                        if (p_index_bottom_left == UINT32_MAX|| p_index_top_left == UINT32_MAX) {
                            return -1; 
                        }
                        if (p->box.r < chunk_ref_bottom.chunk->box.l) { // right edge crossed left 
                            chunk_pop(&chunk_ref_top); 
                            chunk_pop(&chunk_ref_bottom); 
                            particle_update_chunk_ref(p, 2, chunk_top_left, p_index_top_left); 
                            particle_update_chunk_ref(p, 3, chunk_bottom_left, p_index_bottom_left); 
                        } else {
                            p->chunk_state = LRTB; 
                            Chunk* chunk_top_right = chunk_ref_top.chunk; 
                            Chunk* chunk_bottom_right = chunk_ref_bottom.chunk; 
                            uint32_t p_index_top_right = chunk_ref_top.p_index;
                            uint32_t p_index_bottom_right = chunk_ref_bottom.p_index;
                            particle_update_chunk_ref(p, 0, chunk_bottom_right, p_index_bottom_right); 
                            particle_update_chunk_ref(p, 1, chunk_top_right, p_index_top_right); 
                            particle_update_chunk_ref(p, 2, chunk_top_left, p_index_top_left); 
                            particle_update_chunk_ref(p, 3, chunk_bottom_left, p_index_bottom_left); 
                        }
                    } else {
                        p->v.x *= -1; 
                    }
                } else if (p->box.t < chunk_ref_top.chunk->box.b) { // top edge crossed bottom of top chunk 
                    uint32_t p_index = chunk_ref_bottom.p_index; 
                    if (p_index < 0) {
                        return -1; 
                    }
                    p->chunk_state = ONE; 
                    chunk_pop(&chunk_ref_top); 
                    Chunk* chunk = chunk_ref_bottom.chunk;
                    particle_update_chunk_ref(p, 0, chunk, p_index); 
                } else if (p->box.b > chunk_ref_bottom.chunk->box.t) { // bottom edge crossed top of bottom chunk
                    uint32_t p_index = chunk_ref_top.p_index; 
                    if (p_index < 0) {
                        return -1; 
                    }
                    p->chunk_state = ONE; 
                    chunk_pop(&chunk_ref_bottom); 
                    Chunk* chunk = chunk_ref_top.chunk;
                    particle_update_chunk_ref(p, 0, chunk, p_index); 
                }
            } break;
            case LEFT_RIGHT: {
                ChunkRef chunk_ref_left = p->chunk_ref[0]; 
                ChunkRef chunk_ref_right = p->chunk_ref[1]; 
                particle_collisions(p, &chunk_ref_left);
                particle_collisions(p, &chunk_ref_right);
                if (p->box.t > chunk_ref_left.chunk->box.t) { // top edge crossed top  
                    if (chunk_ref_left.chunk->top != NULL) {
                        Chunk* chunk_top_left = chunk_ref_left.chunk->top; 
                        Chunk* chunk_top_right = chunk_ref_right.chunk->top; 
                        uint32_t p_index_top_left = chunk_append(chunk_top_left, p); 
                        uint32_t p_index_top_right = chunk_append(chunk_top_right, p); 
                        if (p_index_top_left == UINT32_MAX || p_index_top_right == UINT32_MAX) {
                            return -1; 
                        }
                        if (p->box.b > chunk_ref_left.chunk->box.t) { // bottom edge crossed top 
                            chunk_pop(&chunk_ref_left); 
                            chunk_pop(&chunk_ref_right); 
                            particle_update_chunk_ref(p, 0, chunk_top_left, p_index_top_left); 
                            particle_update_chunk_ref(p, 1, chunk_top_right, p_index_top_right); 
                        } else { 
                            p->chunk_state = LRTB; 
                            Chunk* chunk_bottom_left = chunk_ref_left.chunk; 
                            Chunk* chunk_bottom_right = chunk_ref_right.chunk; 
                            uint32_t p_index_bottom_left = chunk_ref_left.p_index;
                            uint32_t p_index_bottom_right = chunk_ref_right.p_index;
                            particle_update_chunk_ref(p, 0, chunk_bottom_right, p_index_bottom_right); 
                            particle_update_chunk_ref(p, 1, chunk_top_right, p_index_top_right); 
                            particle_update_chunk_ref(p, 2, chunk_top_left, p_index_top_left); 
                            particle_update_chunk_ref(p, 3, chunk_bottom_left, p_index_bottom_left); 
                        }
                    } else {
                        p->v.y *= -1; 
                    }
                } else if (p->box.b < chunk_ref_left.chunk->box.b) { // bottom edge crossed bottom 
                    if (chunk_ref_left.chunk->bottom != NULL) {
                        Chunk* chunk_bottom_left = chunk_ref_left.chunk->bottom; 
                        Chunk* chunk_bottom_right = chunk_ref_right.chunk->bottom; 
                        uint32_t p_index_bottom_left = chunk_append(chunk_bottom_left, p); 
                        uint32_t p_index_bottom_right = chunk_append(chunk_bottom_right, p); 
                        if (p_index_bottom_left == UINT32_MAX || p_index_bottom_right == UINT32_MAX) {
                            return -1; 
                        }
                        if (p->box.t < chunk_ref_left.chunk->box.b) { // top edge crossed bottom  
                            chunk_pop(&chunk_ref_left); 
                            chunk_pop(&chunk_ref_right); 
                            particle_update_chunk_ref(p, 0, chunk_bottom_left, p_index_bottom_left); 
                            particle_update_chunk_ref(p, 1, chunk_bottom_right, p_index_bottom_right); 
                        } else { 
                            p->chunk_state = LRTB; 
                            Chunk* chunk_top_left = chunk_ref_left.chunk; 
                            Chunk* chunk_top_right = chunk_ref_right.chunk; 
                            uint32_t p_index_top_left = chunk_ref_left.p_index;
                            uint32_t p_index_top_right = chunk_ref_right.p_index;
                            particle_update_chunk_ref(p, 0, chunk_bottom_right, p_index_bottom_right); 
                            particle_update_chunk_ref(p, 1, chunk_top_right, p_index_top_right); 
                            particle_update_chunk_ref(p, 2, chunk_top_left, p_index_top_left); 
                            particle_update_chunk_ref(p, 3, chunk_bottom_left, p_index_bottom_left); 
                        }
                    } else {
                        p->v.y *= -1; 
                    }
                } else if (p->box.r < chunk_ref_right.chunk->box.l) { // right edge crossed left of right chunk  
                    p->chunk_state = ONE; 
                    chunk_pop(&chunk_ref_right); 
                    particle_update_chunk_ref(p, 0, chunk_ref_left.chunk, chunk_ref_left.p_index);
                } else if (p->box.l > chunk_ref_left.chunk->box.r) { // left edge crossed right of left chunk 
                    p->chunk_state = ONE; 
                    chunk_pop(&chunk_ref_left); 
                    particle_update_chunk_ref(p, 0, chunk_ref_right.chunk, chunk_ref_right.p_index);
                }
            } break;
            case LRTB: {
                ChunkRef chunk_ref_bottom_right = p->chunk_ref[0]; 
                ChunkRef chunk_ref_top_right = p->chunk_ref[1]; 
                ChunkRef chunk_ref_top_left = p->chunk_ref[2]; 
                ChunkRef chunk_ref_bottom_left = p->chunk_ref[3]; 
                particle_collisions(p, &chunk_ref_bottom_right);
                particle_collisions(p, &chunk_ref_top_right);
                particle_collisions(p, &chunk_ref_top_left);
                particle_collisions(p, &chunk_ref_bottom_left);
                if (p->box.l > chunk_ref_top_left.chunk->box.r) { // left edge crossed right of left
                    p->chunk_state = TOP_BOTTOM;
                    chunk_pop(&chunk_ref_top_left); 
                    chunk_pop(&chunk_ref_bottom_left); 
                    particle_update_chunk_ref(p, 2, chunk_ref_top_right.chunk, chunk_ref_top_right.p_index); 
                    particle_update_chunk_ref(p, 3, chunk_ref_bottom_right.chunk, chunk_ref_bottom_right.p_index); 
                } else if (p->box.r < chunk_ref_top_right.chunk->box.l) { // right edge crossed left of right 
                    p->chunk_state = TOP_BOTTOM;
                    chunk_pop(&chunk_ref_top_right); 
                    chunk_pop(&chunk_ref_bottom_right); 
                    particle_update_chunk_ref(p, 2, chunk_ref_top_left.chunk, chunk_ref_top_left.p_index); 
                    particle_update_chunk_ref(p, 3, chunk_ref_bottom_left.chunk, chunk_ref_bottom_left.p_index); 
                } else if (p->box.b > chunk_ref_bottom_left.chunk->box.t) { // bottom edge crossed top of bottom 
                    p->chunk_state = LEFT_RIGHT;
                    chunk_pop(&chunk_ref_bottom_left); 
                    chunk_pop(&chunk_ref_bottom_right); 
                    particle_update_chunk_ref(p, 0, chunk_ref_top_left.chunk, chunk_ref_top_left.p_index); 
                    particle_update_chunk_ref(p, 1, chunk_ref_top_right.chunk, chunk_ref_bottom_left.p_index); 
                } else if (p->box.t < chunk_ref_top_left.chunk->box.b) { // top edge crossed bottom of top 
                    p->chunk_state = LEFT_RIGHT;
                    chunk_pop(&chunk_ref_top_left); 
                    chunk_pop(&chunk_ref_top_right); 
                    particle_update_chunk_ref(p, 0, chunk_ref_bottom_left.chunk, chunk_ref_bottom_left.p_index); 
                    particle_update_chunk_ref(p, 1, chunk_ref_bottom_right.chunk, chunk_ref_bottom_right.p_index); 
                }
            } break;
            default: {
                fprintf(stderr, "ERROR: invalid chunk_state: %d\n", p->chunk_state);
                return -1; 
            } break;
        }
    }
    return 0;
}


int setup_simulation_memory(
    void** ptr_mem_block, 
    Chunk**** ptr_chunkmap, // im a four star programmer, but i think this is really necessary, no? 
    ChunkmapInfo chunkmap_info,
    Chunk*** ptr_border_chunks,
    uint32_t n_border_chunks,
    uint32_t n_max_particles_per_chunk,
    Particle** ptr_particles,
    uint32_t n_particles) {

    uint32_t nx = chunkmap_info.n.x; 
    uint32_t ny = chunkmap_info.n.y; 
    size_t total_size = 
        n_border_chunks * sizeof(Chunk*) +
        nx * sizeof(Chunk*) +
        nx * ny * sizeof(Chunk*) +
        nx * ny * sizeof(Chunk) + 
        nx * ny * n_max_particles_per_chunk * sizeof(Particle*) +
        n_particles * sizeof(Particle);

    void* mem_block = (void*) malloc(total_size);
    if (mem_block == NULL) {
        fprintf(stderr, "ERROR: malloc of memory block (size=%zu) failed.\n", total_size);
        return -1;
    }
    printf("Allocated %zuKB on heap.\n", total_size/1000);

    // heap layout: 
    // |  abstraction  |  type            |  n                                |  description            |
    // | ------------- | ---------------- | --------------------------------- | ----------------------- | 
    // |  chunkmap     |  Chunk**         |  number of chunks in x direction  |  chunk column pointers  |
    // |               |  Chunk*          |  number of chunks in y direction  |  chunk column pointers  |
    // |               |  Chunk           |  1                                |  This repeats>          |
    // |               |  Particle*       |  n_max_particles_per_chunk        |  n_chunks times>        |
    // |               |  ...             |  ...                              |  ...                    |
    // |  particles    |  Particle        |  n_particles                      |                         | 

    Chunk** border_chunks = (Chunk**)mem_block;
    Chunk*** chunkmap = (Chunk***)(border_chunks + n_border_chunks);

    for (uint32_t i = 0; i < nx; i++) {
        chunkmap[i] = (Chunk**)(chunkmap + nx + i * ny);
        for (uint32_t j = 0; j < ny; j++) {
            chunkmap[i][j] = (Chunk*)((char*)(chunkmap + nx + nx * ny) + (j + i * ny) * (sizeof(Chunk) + n_max_particles_per_chunk * sizeof(Particle*))); 
            Chunk* chunk = chunkmap[i][j];
            chunk->particles = (Particle**) ((char*)chunkmap[i][j] + sizeof(Chunk));  
            memset(chunk->particles, 0, n_max_particles_per_chunk * sizeof(Particle*));
            chunk->box.l = i*chunkmap_info.size.x; 
            chunk->box.r = (i+1)*chunkmap_info.size.x;
            chunk->box.b = j*chunkmap_info.size.y;
            chunk->box.t = (j+1)*chunkmap_info.size.y;
            chunk->left = NULL; 
            chunk->right = NULL; 
            chunk->bottom = NULL; 
            chunk->top = NULL; 
            chunk->n_filled = 0; 
            chunk->n_free = n_max_particles_per_chunk;
            chunk->xy.x = i; 
            chunk->xy.y = j; 
        }
    }

    uint32_t i_border_chunks = 0; 
    for (uint32_t i = 0; i < nx; i++) {
        for (uint32_t j = 0; j < ny; j++) {
            bool is_border_chunk = false; 
            if (i > 0) 
                chunkmap[i][j]->left = chunkmap[i-1][j];
            else if (!is_border_chunk) {
                border_chunks[i_border_chunks] = chunkmap[i][j]; 
                i_border_chunks++; 
                is_border_chunk = true; 
            }
            if (j > 0) 
                chunkmap[i][j]->bottom = chunkmap[i][j-1];
            else if (!is_border_chunk) {
                border_chunks[i_border_chunks] = chunkmap[i][j]; 
                i_border_chunks++; 
                is_border_chunk = true; 
            }
            if (i + 1 < chunkmap_info.n.x) 
                chunkmap[i][j]->right = chunkmap[i+1][j];
            else if (!is_border_chunk) {
                border_chunks[i_border_chunks] = chunkmap[i][j]; 
                i_border_chunks++; 
                is_border_chunk = true; 
            }
            if (j + 1 < chunkmap_info.n.y) 
                chunkmap[i][j]->top = chunkmap[i][j+1];
            else if (!is_border_chunk) {
                border_chunks[i_border_chunks] = chunkmap[i][j]; 
                i_border_chunks++; 
                is_border_chunk = true; 
            }
        }
    }
    if (n_border_chunks != i_border_chunks) {
        fprintf(stderr, "ERROR: n_border_chunks is wrong. %d %d\n", i_border_chunks, n_border_chunks);
        free(mem_block); 
        return -1; 
    }
    Particle* particles = (Particle*) ((char*)chunkmap[nx-1][ny-1] + n_max_particles_per_chunk * sizeof(Particle*) + sizeof(Chunk)); 
    memset(particles, 0, n_particles * sizeof(Particle));  
    *ptr_mem_block = (void*) mem_block;  
    *ptr_border_chunks = (Chunk**) border_chunks; 
    *ptr_chunkmap = (Chunk***) chunkmap; 
    *ptr_particles = (Particle*) particles; 
    return 0; 
}


int setup_particles(
    Chunk*** chunkmap, 
    ChunkmapInfo chunkmap_info, 
    Particle* particles, 
    uint32_t n_particles, 
    float particle_radius, 
    Container* container) {

    uint32_t particles_per_row = 1.0f/(particle_radius*container->scalar);
    uint32_t particles_per_col = 1.0f/(particle_radius*container->zoom);

    uint32_t n_particles_possible = particles_per_row*particles_per_col;
    if (n_particles_possible < n_particles) {
        fprintf(stderr, "Too many particles %d for container %d\n", n_particles, n_particles_possible); 
        return -1; 
    }

    float v_start = 1000.0f; 
    for (uint32_t i = 0; i < n_particles; i++) { 
        Particle* p = &particles[i]; 

        uint32_t col = i%particles_per_row;
        uint32_t row = (uint) (i/particles_per_row);
        p->p.x = particle_radius*(1.0f + 2.0f*col); 
        p->p.y = particle_radius*(1.0f + 2.0f*row); 

        p->box.l = p->p.x-particle_radius; 
        p->box.r = p->p.x+particle_radius;
        p->box.b = p->p.y-particle_radius;
        p->box.t = p->p.y+particle_radius; 

        p->gpu.x = -1.0f + p->p.x * container->scalar; 
        p->gpu.y = -1.0f + p->p.y * container->zoom;
        /* p->gpu.z = 0.0f; */ 

        p->v.x = rand_float(-v_start, v_start); 
        p->v.y = rand_float(-v_start, v_start); 
        /* p->v.x = 1000.0f; */  
        /* p->v.y = 0.0f; */  

    }

    for (uint32_t i = 0; i < chunkmap_info.n.x; i++) {
        for (uint32_t j = 0; j < chunkmap_info.n.y; j++) {
            Chunk* chunk = chunkmap[i][j]; 
            for (uint32_t k = 0; k < n_particles; k++) {
                Particle* p = &particles[k]; 
                if (box_overlap(p->box, chunk->box)) {
                    switch (p->chunk_state) {
                        case INVALID: {
                            uint32_t p_index = chunk_append(chunk, p);
                            if (p_index == UINT32_MAX) {
                                fprintf(stderr, "ERROR: chunk_append failed.\n");
                                return -1; 
                            }
                            p->chunk_state = ONE; 
                            particle_update_chunk_ref(p, 0, chunk, p_index); 
                        } break; 
                        case ONE: {
                              if (p->chunk_ref[0].chunk->right == chunk) { // the way we iterate, we only have to check if its a chunk to the right  
                                    uint32_t p_index = chunk_append(chunk, p);
                                    if (p_index == UINT32_MAX) {
                                        fprintf(stderr, "ERROR: chunk_append failed.\n");
                                        return -1; 
                                    }
                                    p->chunk_state = LEFT_RIGHT; 
                                    particle_update_chunk_ref(p, 1, chunk, p_index); 
                              } else if (p->chunk_ref[0].chunk->top == chunk) {
                                    uint32_t p_index = chunk_append(chunk, p);
                                    if (p_index == UINT32_MAX) {
                                        fprintf(stderr, "ERROR: chunk_append failed.\n");
                                        return -1; 
                                    }
                                    p->chunk_state = TOP_BOTTOM; 
                                    particle_update_chunk_ref(p, 2, chunk, p_index); 
                                    particle_update_chunk_ref(p, 3, p->chunk_ref[0].chunk, p->chunk_ref[0].p_index); 
                              }
                        } break; 
                        case TOP_BOTTOM: { 
                            Chunk* chunk_top_right = p->chunk_ref[2].chunk->right; 
                            Chunk* chunk_bottom_right = p->chunk_ref[3].chunk->right; 
                            uint32_t p_index_top_right = chunk_append(chunk_top_right, p);
                            uint32_t p_index_bottom_right = chunk_append(chunk_bottom_right, p);
                            if (p_index_top_right == UINT32_MAX || p_index_bottom_right == UINT32_MAX) {
                                fprintf(stderr, "ERROR: chunk_append failed.\n");
                                return -1; 
                            }
                            p->chunk_state = LRTB; 
                            particle_update_chunk_ref(p, 0, chunk_bottom_right, p_index_bottom_right);
                            particle_update_chunk_ref(p, 1, chunk_top_right, p_index_top_right);
                        } break; 
                        case LEFT_RIGHT: { 
                            Chunk* chunk_top_left = p->chunk_ref[0].chunk->top; 
                            Chunk* chunk_top_right = p->chunk_ref[1].chunk->top; 
                            Chunk* chunk_bottom_right = p->chunk_ref[1].chunk; 
                            Chunk* chunk_bottom_left = p->chunk_ref[0].chunk; 

                            uint32_t p_index_bottom_right = p->chunk_ref[1].p_index; 
                            uint32_t p_index_bottom_left = p->chunk_ref[0].p_index; 
                            uint32_t p_index_top_left = chunk_append(chunk_top_left, p);
                            uint32_t p_index_top_right = chunk_append(chunk_top_right, p);
                            if (p_index_top_left == UINT32_MAX || p_index_top_right == UINT32_MAX) {
                                fprintf(stderr, "ERROR: chunk_append failed.\n");
                                return -1; 
                            }
                            p->chunk_state = LRTB; 
                            particle_update_chunk_ref(p, 0, chunk_bottom_right, p_index_bottom_right); 
                            particle_update_chunk_ref(p, 1, chunk_top_right, p_index_top_right); 
                            particle_update_chunk_ref(p, 2, chunk_top_left, p_index_top_left); 
                            particle_update_chunk_ref(p, 3, chunk_bottom_left, p_index_bottom_left); 
                        } break; 
                        case LRTB: { // nothing to do here 
                        } break; 
                    }
                }
            }
        } 
    } 
    return 0; 
}


int main(int argc, char* argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "ERROR: SDL_Init failed: %s\n", SDL_GetError());
        return 1; 
    } 
    const char* WINDOW_TITLE = "Pressure Simulation";
    SDL_Window* window = SDL_CreateWindow(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_VULKAN);
    if (window == NULL) {
        fprintf(stderr, "ERROR: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;  
    }

    SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL); 
    if (device == NULL) {
        fprintf(stderr, "ERROR: SDL_CreateGPUDevice failed: %s\n", SDL_GetError());
        return 1; 
    }

    fprintf(stdout, "OK: Created device with driver '%s'\n", SDL_GetGPUDeviceDriver(device));
    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        fprintf(stderr, "ERROR: SDL_ClaimWindowForGPUDevice failed: %s\n", SDL_GetError());
        return 1; 
    }

    //
    // ----  Vulkan setup ---- 
    //

    SDL_GPUTextureFormat depth_stencil_format;

    if (SDL_GPUTextureSupportsFormat(
            device, 
            SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    } else if (SDL_GPUTextureSupportsFormat(
            device,
            SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    } else {
        fprintf(stderr, "ERROR: Stencil formats not supported!\n");
        return -1;
    }

    SDL_GPUShader* particles_shader_vert = load_shader(device, "shaders/compiled/Circle.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 1, 0); 
    if (particles_shader_vert == NULL) {
        fprintf(stderr, "ERROR: load_shader failed.\n");
        return 1;   
    }

    SDL_GPUShader* particles_shader_frag = load_shader(device, "shaders/compiled/Circle.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 0); 
    if (particles_shader_frag == NULL) {
        fprintf(stderr, "ERROR: load_shader failed.\n");
        return 1;   
    }

    SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {
        .vertex_shader = particles_shader_vert,
        .fragment_shader = particles_shader_frag,
        .vertex_input_state = (SDL_GPUVertexInputState) {
            .vertex_buffer_descriptions = (SDL_GPUVertexBufferDescription[]) {
                {
                    .slot = 0,
                    .pitch = sizeof(PositionTextureVertex),
                    .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
                    .instance_step_rate = 0
                },
            }, 
            .num_vertex_buffers = 1,
            .vertex_attributes = (SDL_GPUVertexAttribute[]) {
                {
                    .location = 0,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,  
                    .offset = 0                
                }, 
                {
                    .location = 1,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                    .offset = sizeof(float) * 3
                },
                {
                    .location = 2,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
                    .offset = sizeof(float) * 3 + sizeof(float) * 2
                }, 
                {
                    .location = 3,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
                    .offset = sizeof(float) * 3 + sizeof(float) * 2 + sizeof(uint8_t) * 4 
                }
            },  
            .num_vertex_attributes = 4
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = (SDL_GPURasterizerState){
            .cull_mode = SDL_GPU_CULLMODE_NONE,
            .fill_mode = SDL_GPU_FILLMODE_FILL,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE
        },
        .target_info = {
            .color_target_descriptions = (SDL_GPUColorTargetDescription[]){
                {
                    .format = SDL_GetGPUSwapchainTextureFormat(device, window),
                    .blend_state = (SDL_GPUColorTargetBlendState) {
                        .src_color_blendfactor   = SDL_GPU_BLENDFACTOR_SRC_ALPHA, 
                        .dst_color_blendfactor   = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, 
                        .color_blend_op          = SDL_GPU_BLENDOP_ADD, 
                        .src_alpha_blendfactor   = SDL_GPU_BLENDFACTOR_SRC_ALPHA, 
                        .dst_alpha_blendfactor   = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, 
                        .alpha_blend_op          = SDL_GPU_BLENDOP_ADD, 
                        /* .color_write_mask        = SDL_GPU_COLORCOMPONENT_A, */ 
                        .enable_blend            = true, 
                        /* .enable_color_write_mask = true, */
                        /* .padding1                = 0, */ 
                        /* .padding2                = 0 */ 
                    }
                }
            },
            .num_color_targets = 1
        },
    };  

    SDL_GPUGraphicsPipeline* particles_pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipeline_info);
    if (particles_pipeline == NULL) {
        fprintf(stderr, "ERROR: SDL_CreateGPUGraphicsPipeline failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_ReleaseGPUShader(device, particles_shader_vert); 
    SDL_ReleaseGPUShader(device, particles_shader_frag); 

    SDL_GPUShader* debug_lines_shader_vert = load_shader(device, "shaders/compiled/Line.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 1, 0); 
    if (particles_shader_vert == NULL) {
        fprintf(stderr, "ERROR: load_shader failed.\n");
        return 1;   
    }

    SDL_GPUShader* debug_lines_shader_frag = load_shader(device, "shaders/compiled/SolidColor.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 0); 
    if (particles_shader_frag == NULL) {
        fprintf(stderr, "ERROR: load_shader failed.\n");
        return 1;   
    }

    SDL_GPUGraphicsPipelineCreateInfo debug_lines_pipeline_info = {
        .vertex_shader = debug_lines_shader_vert,
        .fragment_shader = debug_lines_shader_frag,
        .vertex_input_state = (SDL_GPUVertexInputState) {
            .vertex_buffer_descriptions = (SDL_GPUVertexBufferDescription[]) {
                {
                    .slot = 0,
                    .pitch = sizeof(Vec2Vertex),
                    .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
                    .instance_step_rate = 0
                },
            }, 
            .num_vertex_buffers = 1,
            .vertex_attributes = (SDL_GPUVertexAttribute[]) {
                {
                    .location = 0,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,  
                    .offset = 0                
                }
            },  
            .num_vertex_attributes = 1
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST,
        .rasterizer_state = (SDL_GPURasterizerState){
            .cull_mode = SDL_GPU_CULLMODE_NONE,
            .fill_mode = SDL_GPU_FILLMODE_LINE,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE
        },
        /* .depth_stencil_state = (SDL_GPUDepthStencilState){ */
        /*  .front_stencil_state = (SDL_GPUStencilOpState){ */
        /*      .compare_op = SDL_GPU_COMPAREOP_NEVER, */
        /*      .fail_op = SDL_GPU_STENCILOP_REPLACE, */
        /*      .pass_op = SDL_GPU_STENCILOP_KEEP, */
        /*      .depth_fail_op = SDL_GPU_STENCILOP_KEEP, */
        /*  }, */
        /*  .back_stencil_state = (SDL_GPUStencilOpState){ */
        /*      .compare_op = SDL_GPU_COMPAREOP_NEVER, */
        /*      .fail_op = SDL_GPU_STENCILOP_REPLACE, */
        /*      .pass_op = SDL_GPU_STENCILOP_KEEP, */
        /*      .depth_fail_op = SDL_GPU_STENCILOP_KEEP, */
        /*  }, */
        /*  .compare_op = SDL_GPU_COMPAREOP_NEVER, */ 
        /*  .write_mask = 0xFF, */
        /*  .enable_depth_test = true, */
        /* }, */
        .target_info = {
            .color_target_descriptions = (SDL_GPUColorTargetDescription[]){
                {
                    .format = SDL_GetGPUSwapchainTextureFormat(device, window),
                    .blend_state = (SDL_GPUColorTargetBlendState) {
                        .src_color_blendfactor   = SDL_GPU_BLENDFACTOR_SRC_ALPHA, 
                        .dst_color_blendfactor   = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, 
                        .color_blend_op          = SDL_GPU_BLENDOP_ADD, 
                        .src_alpha_blendfactor   = SDL_GPU_BLENDFACTOR_SRC_ALPHA, 
                        .dst_alpha_blendfactor   = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, 
                        .alpha_blend_op          = SDL_GPU_BLENDOP_ADD, 
                        /* .color_write_mask        = SDL_GPU_COLORCOMPONENT_A, */ 
                        .enable_blend            = true, 
                        /* .enable_color_write_mask = true, */
                        /* .padding1                = 0, */ 
                        /* .padding2                = 0 */ 
                    }
                }
            },
            .num_color_targets = 1,
            /* .depth_stencil_format = depth_stencil_format, */ 
            /* .has_depth_stencil_target = true */
        },
    };  

    SDL_GPUGraphicsPipeline* debug_lines_pipeline = SDL_CreateGPUGraphicsPipeline(device, &debug_lines_pipeline_info);
    if (debug_lines_pipeline == NULL) {
        fprintf(stderr, "ERROR: SDL_CreateGPUGraphicsPipeline failed: %s\n", SDL_GetError());
        return 1;
    }

    /* debug_lines_pipeline_info.depth_stencil_state = (SDL_GPUDepthStencilState){ */
    /*  .enable_stencil_test = true, */
    /*  .front_stencil_state = (SDL_GPUStencilOpState){ */
    /*      .compare_op = SDL_GPU_COMPAREOP_EQUAL, */
    /*      .fail_op = SDL_GPU_STENCILOP_KEEP, */
    /*      .pass_op = SDL_GPU_STENCILOP_KEEP, */
    /*      .depth_fail_op = SDL_GPU_STENCILOP_KEEP, */
    /*  }, */
    /*  .back_stencil_state = (SDL_GPUStencilOpState){ */
    /*      .compare_op = SDL_GPU_COMPAREOP_NEVER, */
    /*      .fail_op = SDL_GPU_STENCILOP_KEEP, */
    /*      .pass_op = SDL_GPU_STENCILOP_KEEP, */
    /*      .depth_fail_op = SDL_GPU_STENCILOP_KEEP, */
    /*  }, */
    /*  .compare_mask = 0xFF, */
    /*  .write_mask = 0 */
    /* }; */
    SDL_GPUGraphicsPipeline* debug_pipeline_maskee = SDL_CreateGPUGraphicsPipeline(device, &debug_lines_pipeline_info);
    if (debug_pipeline_maskee == NULL)
    {
        fprintf(stderr, "Failed to create maskee pipeline!\n");
        return -1;
    }

    SDL_ReleaseGPUShader(device, debug_lines_shader_vert); 
    SDL_ReleaseGPUShader(device, debug_lines_shader_frag); 

    SDL_GPUTexture* texture_depth_stencil = SDL_CreateGPUTexture(
        device,
        &(SDL_GPUTextureCreateInfo) {
            .type = SDL_GPU_TEXTURETYPE_2D,
            .width = WINDOW_WIDTH,
            .height = WINDOW_WIDTH,
            .layer_count_or_depth = 1,
            .num_levels = 1,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .format = depth_stencil_format,
            .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
        }
    );

    // ---- [START] vulkan particle setup ----
    SDL_GPUBuffer*          particles_vertex_buffer = NULL; 
    SDL_GPUBuffer*          particles_index_buffer = NULL; 
    SDL_GPUTransferBuffer*  particles_transfer_buffer = NULL; 
    PositionTextureVertex*  particles_vertex_data = NULL; 

    uint32_t particles_n_vertices = 4; 
    uint32_t particles_n_indices  = 6; 
    vulkan_buffers_create(device, &particles_vertex_buffer, sizeof *particles_vertex_data, particles_n_vertices, &particles_index_buffer, particles_n_indices, &particles_transfer_buffer, (void**)&particles_vertex_data);
    
    particles_vertex_data[0] = (PositionTextureVertex) { 
         -1.0f,  1.0f,  0.0f, 
          0.0f,  1.0f, 
         COLOR_TO_UINT8(COLOR_RED),
         COLOR_TO_UINT8(COLOR_TRANSPARENT)
    };
    particles_vertex_data[1] = (PositionTextureVertex) {  
        1.0f,  1.0f,  0.0f, 
        1.0f,  1.0f, 
        COLOR_TO_UINT8(COLOR_GREEN),
        COLOR_TO_UINT8(COLOR_TRANSPARENT)
    };
    particles_vertex_data[2] = (PositionTextureVertex) {  
         1.0f, -1.0f,  0.0f, 
         1.0f,  0.0f, 
        COLOR_TO_UINT8(COLOR_BLUE),
        COLOR_TO_UINT8(COLOR_TRANSPARENT)
    };
    particles_vertex_data[3] = (PositionTextureVertex) { 
        -1.0f, -1.0f,  0.0f, 
         0.0f,  0.0f, 
        COLOR_TO_UINT8(COLOR_PINK),
        COLOR_TO_UINT8(COLOR_TRANSPARENT)
    };

    Container container = { 
        .width = WINDOW_WIDTH, 
        .height = WINDOW_HEIGHT, 
        .zoom = 1/500.0f 
    }; 

    container.inverse_aspect_ratio = (float) container.height/container.width; 
    container.scalar = container.inverse_aspect_ratio * container.zoom; 
    float particle_radius = R; 
    for (int i = 0; i < particles_n_vertices; i++) {
        particles_vertex_data[i].x *= container.inverse_aspect_ratio;   
        particles_vertex_data[i].x *= particle_radius*container.zoom;  
        particles_vertex_data[i].y *= particle_radius*container.zoom;  
    }

    Uint16* index_data = (Uint16*) &particles_vertex_data[particles_n_vertices];
    index_data[0] = 2;
    index_data[1] = 1;
    index_data[2] = 0;
    index_data[3] = 2;
    index_data[4] = 0;
    index_data[5] = 3;

    vulkan_buffers_upload(device, particles_vertex_buffer, sizeof(PositionTextureVertex), particles_n_vertices, particles_index_buffer, particles_n_indices, particles_transfer_buffer);

    uint32_t n_particles = N; // Depends on the GPU i guess, I have 8GB VRAM so 1 million should be fine 
    SDL_GPUBuffer* particles_sso_buffer = SDL_CreateGPUBuffer(
        device,
        &(SDL_GPUBufferCreateInfo) {
            .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
            .size = n_particles * sizeof(GPUParticle)
        }
    );

    SDL_GPUTransferBuffer* particles_sso_transfer_buffer = SDL_CreateGPUTransferBuffer(
        device,
        &(SDL_GPUTransferBufferCreateInfo) {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = n_particles * sizeof(GPUParticle)
        }
    );
    // ---- [END] vulkan particle setup ----

    // ---- [START] vulkan debug setup ----
    SDL_GPUBuffer*          debug_lines_vertex_buffer = NULL; 
    SDL_GPUBuffer*          debug_lines_index_buffer = NULL; 
    SDL_GPUTransferBuffer*  debug_lines_transfer_buffer = NULL; 
    Vec2Vertex*             debug_lines_vertex_data = NULL; 

    uint32_t debug_lines_n_vertices = 4; 
    uint32_t debug_lines_n_indices  = 4; 
    ChunkmapInfo chunkmap_info = (ChunkmapInfo) {.n = (Vec2i) {.x = 20, .y = 16}, .size = (Vec2f) { .x = 0.0f, .y = 0.0f }};
    chunkmap_info.size.x = (float) container.width / chunkmap_info.n.x; 
    chunkmap_info.size.y = (float) container.height / chunkmap_info.n.y; 
    uint32_t n_lines = chunkmap_info.n.x + chunkmap_info.n.y - 2; 
    vulkan_buffers_create(device, &debug_lines_vertex_buffer, sizeof(Vec2Vertex), debug_lines_n_vertices, &debug_lines_index_buffer, debug_lines_n_indices, &debug_lines_transfer_buffer, (void**)&debug_lines_vertex_data);

    debug_lines_vertex_data[0] = (Vec2Vertex) { -1.0f, -1.0f };
    debug_lines_vertex_data[1] = (Vec2Vertex) { -1.0f,  1.0f };
    debug_lines_vertex_data[2] = (Vec2Vertex) { -1.0f, -1.0f };
    debug_lines_vertex_data[3] = (Vec2Vertex) {  1.0f, -1.0f };

    for (int i = 0; i < debug_lines_n_vertices; i++) {
        debug_lines_vertex_data[i].x *= container.inverse_aspect_ratio;   
        debug_lines_vertex_data[i].x *= ((float)container.width/2)*container.zoom;  
        debug_lines_vertex_data[i].y *= ((float)container.height/2)*container.zoom;  
    }

    Uint16* debug_lines_index_data = (Uint16*) &debug_lines_vertex_data[debug_lines_n_vertices];
    debug_lines_index_data[0] = 0;
    debug_lines_index_data[1] = 1;
    debug_lines_index_data[2] = 2;
    debug_lines_index_data[3] = 3;

    vulkan_buffers_upload(device, debug_lines_vertex_buffer, sizeof *debug_lines_vertex_data, debug_lines_n_vertices, debug_lines_index_buffer, debug_lines_n_indices, debug_lines_transfer_buffer);

    SDL_GPUBuffer* debug_lines_sso_buffer = SDL_CreateGPUBuffer(
        device,
        &(SDL_GPUBufferCreateInfo) {
            .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
            .size = n_lines * sizeof(GPULine)
        }
    );

    SDL_GPUTransferBuffer* debug_lines_sso_transfer_buffer = SDL_CreateGPUTransferBuffer(
        device,
        &(SDL_GPUTransferBufferCreateInfo) {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = n_lines * sizeof(GPULine)
        }
    );
    

    // ---- [END] vulkan debug setup ----

    // 
    // ---- [END] vulkan setup -----
    // 

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    uint32_t viewport_width  = WINDOW_WIDTH; 
    uint32_t viewport_height = WINDOW_HEIGHT; 
    float viewport_min_depth = 0.1f; 
    float viewport_max_depth = 1.0f; 
    SDL_GPUViewport small_viewport = (SDL_GPUViewport) { 
        (WINDOW_WIDTH-viewport_width)/2.0f, (WINDOW_HEIGHT-viewport_height)/2.0f, 
        viewport_width, viewport_height, 
        viewport_min_depth, viewport_max_depth 
    };

    // Setup simulation 
    void* mem_block = NULL; 

    Chunk*** chunkmap = NULL;

    Chunk** border_chunks = NULL;
    uint32_t n_border_chunks = 2 * (chunkmap_info.n.x+chunkmap_info.n.y) - 4; 
    uint32_t n_max_particles_per_chunk = 2 * chunkmap_info.size.x * chunkmap_info.size.y / (particle_radius * particle_radius); 
    printf("chunk_grid: (x:%d,y:%d)\n", chunkmap_info.n.x, chunkmap_info.n.y);
    printf("n_max_particles_per_chunk:%d\n", n_max_particles_per_chunk);

    Particle* particles = NULL; 

    printf("initializing memory...\n");
    Destroyer destroyers[] = {
        (Destroyer){
            .pipeline = particles_pipeline, 
            .vertex_buffer = particles_vertex_buffer, 
            .index_buffer = particles_index_buffer, 
            .sso_buffer = particles_sso_buffer, 
            .sso_transfer_buffer = particles_sso_transfer_buffer
        },
        (Destroyer){
            .pipeline = debug_lines_pipeline, 
            .vertex_buffer = debug_lines_vertex_buffer, 
            .index_buffer = debug_lines_index_buffer, 
            .sso_buffer = debug_lines_sso_buffer, 
            .sso_transfer_buffer = debug_lines_sso_transfer_buffer
        },
    }; 
    if (setup_simulation_memory(&mem_block, &chunkmap, chunkmap_info, &border_chunks, n_border_chunks, n_max_particles_per_chunk, &particles, n_particles) < 0) {
        destroy_sdl(device, window, destroyers, 2, debug_pipeline_maskee, texture_depth_stencil);  
        return 1; 
    }
    printf("memory initialized successfully!\n");

    if (setup_particles(chunkmap, chunkmap_info, particles, n_particles, particle_radius, &container) < 0) {
        fprintf(stderr, "ERROR: sim setup failed.\n");
        free(mem_block);
        destroy_sdl(device, window, destroyers, 2, debug_pipeline_maskee, texture_depth_stencil);  
        return 1; 
    }
    printf("%d particles initialized!\n", n_particles);

    uint32_t sim_state = 0; 
    float dt = 0.001f; 

    bool quit = false; 
    bool debug_mode = true; 

    while (!quit) {
        SDL_Event event;
        if (SDL_PollEvent(&event)) 
            handle_event(event, &quit, &debug_mode, &sim_state); 

        SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(device);
        if (cmdbuf == NULL) {
            fprintf(stderr, "ERROR: SDL_AcquireGPUCommandBuffer failed: %s\n", SDL_GetError());
            break; 
        }

        SDL_GPUTexture* swapchain_texture;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmdbuf, window, &swapchain_texture, NULL, NULL)) {
            fprintf(stderr, "ERROR: SDL_WaitAndAcquireGPUSwapchainTexture failed: %s\n", SDL_GetError());
            break; 
        }

        if (swapchain_texture == NULL) {
            fprintf(stderr, "ERROR: swapchain_texture is NULL.\n");
            SDL_SubmitGPUCommandBuffer(cmdbuf);
            break; 
        }
        
        if (sim_state == 1) {
            if (physics_tick(dt, particles, n_particles, particle_radius, &container, chunkmap, chunkmap_info, border_chunks, n_border_chunks) < 0) {
                fprintf(stderr, "ERROR: physics_tick, stopping simulation.\n"); 
                sim_state = 0; 
            }  
        }


        GPUParticle* live_data = SDL_MapGPUTransferBuffer(device, particles_sso_transfer_buffer, true);
        for (uint32_t i = 0; i < n_particles; i+=1) {
            live_data[i].x = particles[i].gpu.x;
            live_data[i].y = particles[i].gpu.y;
        }
        SDL_UnmapGPUTransferBuffer(device, particles_sso_transfer_buffer); 
        SDL_GPUCopyPass* copy_pass = NULL; 
        copy_pass = SDL_BeginGPUCopyPass(cmdbuf);
        SDL_UploadToGPUBuffer(
            copy_pass,
            &(SDL_GPUTransferBufferLocation) {
                .transfer_buffer = particles_sso_transfer_buffer,
                .offset = 0
            },
            &(SDL_GPUBufferRegion) {
                .buffer = particles_sso_buffer,
                .offset = 0,
                .size = sizeof(GPUParticle) * n_particles
            },
            false   
        );
        SDL_EndGPUCopyPass(copy_pass);


        GPULine* debug_lines_data = SDL_MapGPUTransferBuffer(device, debug_lines_sso_transfer_buffer, true);
        for (uint32_t i = 0; i < chunkmap_info.n.x - 1; i+=1) { // vertical 
            debug_lines_data[i].x = 2 * (float)(i + 1) / (chunkmap_info.n.x); 
            debug_lines_data[i].flags = 0; 
        }
        for (uint32_t i = chunkmap_info.n.x - 1; i < n_lines; i+=1) { // horizontal 
            uint32_t index = i - chunkmap_info.n.x + 1; 
            debug_lines_data[i].y = 2 * (float)(index + 1) / (chunkmap_info.n.y); 
            debug_lines_data[i].flags = 1; 
        }
        SDL_UnmapGPUTransferBuffer(device, debug_lines_sso_transfer_buffer); 
        copy_pass = SDL_BeginGPUCopyPass(cmdbuf);
        SDL_UploadToGPUBuffer(
            copy_pass,
            &(SDL_GPUTransferBufferLocation) {
                .transfer_buffer = debug_lines_sso_transfer_buffer,
                .offset = 0
            },
            &(SDL_GPUBufferRegion) {
                .buffer = debug_lines_sso_buffer,
                .offset = 0,
                .size = sizeof(GPULine) * n_lines 
            },
            false   
        );
        SDL_EndGPUCopyPass(copy_pass);

        SDL_GPUColorTargetInfo color_target_info = { 0 };
        color_target_info.texture     = swapchain_texture;
        color_target_info.clear_color = COLOR_GRAY;  
        color_target_info.load_op     = SDL_GPU_LOADOP_CLEAR;
        color_target_info.store_op    = SDL_GPU_STOREOP_STORE;
        color_target_info.cycle       = false;

        SDL_GPUDepthStencilTargetInfo depth_stencil_target_info = { 0 };
        depth_stencil_target_info.texture           = texture_depth_stencil;
        depth_stencil_target_info.clear_depth       = 0.0f;
        depth_stencil_target_info.load_op           = SDL_GPU_LOADOP_CLEAR;
        depth_stencil_target_info.store_op          = SDL_GPU_STOREOP_DONT_CARE;
        depth_stencil_target_info.stencil_load_op   = SDL_GPU_LOADOP_CLEAR;
        depth_stencil_target_info.stencil_store_op  = SDL_GPU_STOREOP_DONT_CARE;
        depth_stencil_target_info.cycle             = true;
        depth_stencil_target_info.clear_stencil     = 0;

        SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(cmdbuf, &color_target_info, 1, NULL);  // , &depth_stencil_target_info);

        if (debug_mode) {
            /* SDL_SetGPUStencilReference(render_pass, 1); */
            SDL_BindGPUGraphicsPipeline(render_pass, debug_lines_pipeline);
            SDL_SetGPUViewport(render_pass, &small_viewport);
            SDL_BindGPUVertexBuffers(
                render_pass, 
                0, 
                &(SDL_GPUBufferBinding) {
                    .buffer = debug_lines_vertex_buffer, 
                    .offset = 0
                }, 
                1
            ); 
            SDL_BindGPUVertexStorageBuffers(render_pass, 0, &debug_lines_sso_buffer, 1);
            SDL_BindGPUIndexBuffer(
                render_pass, 
                &(SDL_GPUBufferBinding) { 
                    .buffer = debug_lines_index_buffer, 
                    .offset = 0 
                }, 
                SDL_GPU_INDEXELEMENTSIZE_16BIT
            );
            SDL_DrawGPUIndexedPrimitives(render_pass, debug_lines_n_indices, n_lines, 0, 0, 0);

            /* SDL_SetGPUStencilReference(render_pass, 0); */
            /* SDL_BindGPUGraphicsPipeline(render_pass, pipeline_maskee); */
        }

        if (true) {
            SDL_BindGPUGraphicsPipeline(render_pass, particles_pipeline);
            SDL_SetGPUViewport(render_pass, &small_viewport);
            SDL_BindGPUVertexBuffers(
                render_pass, 
                0, 
                &(SDL_GPUBufferBinding) {
                    .buffer = particles_vertex_buffer, 
                    .offset = 0
                }, 
                1
            ); 
            SDL_BindGPUVertexStorageBuffers(
                render_pass,
                0,
                &particles_sso_buffer,
                1
            );
            SDL_BindGPUIndexBuffer(
                render_pass, 
                &(SDL_GPUBufferBinding) { 
                    .buffer = particles_index_buffer, 
                    .offset = 0 
                }, 
                SDL_GPU_INDEXELEMENTSIZE_16BIT
            );
            SDL_DrawGPUIndexedPrimitives(render_pass, particles_n_indices, n_particles, 0, 0, 0);
        }

        SDL_EndGPURenderPass(render_pass);
        SDL_SubmitGPUCommandBuffer(cmdbuf);
    }
    
    free(mem_block);
    destroy_sdl(device, window, destroyers, 2, debug_pipeline_maskee, texture_depth_stencil);  
    return 0; 
}

