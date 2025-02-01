#include "pressure-sim-utils.h"

#include <SDL3/SDL_gpu.h>
#include <stdint.h>
#include <stdlib.h> 
#include <stdio.h> 
#include <string.h>

#define vec2_unpack(_vec) ((_vec).x), ((_vec).y)

#define particle_collisions(_p, _chunk_ref) ({                                           \
    for (uint32_t j = 0; j < (_chunk_ref)->p_index; j++) {                               \
        collide((_p), (_chunk_ref)->chunk->particles[j]);                                \
    }                                                                                    \
    for (uint32_t j = (_chunk_ref)->p_index+1; j < (_chunk_ref)->chunk->n_filled; j++) { \
        collide((_p), (_chunk_ref)->chunk->particles[j]);                                \
    }                                                                                    \
})

#define particle_update_chunk_ref(_p, _i, _chunk, _p_index) ({ \
    (_p)->chunk_ref[(_i)].chunk = (_chunk);                    \
    (_p)->chunk_ref[(_i)].p_index = (_p_index);                \
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
        /* printf("chunk_append (%d,%d)  n_filled=%d,n_free=%d,p->chunk_state=%d\n", (_chunk)->xy.x, (_chunk)->xy.y, (_chunk)->n_filled, (_chunk)->n_free, (_p)->chunk_state); */ \
    } while(0);                                                                \
    _p_index;                                                                  \
})

#define chunk_pop(_chunk_ref) ({                                                                                           \
    if ((_chunk_ref)->chunk->n_filled == 0) {                                                                              \
        fprintf(stderr, "ERROR: popping from empty chunk.\n");                                                             \
        abort();                                                                                                           \
    }                                                                                                                      \
    (_chunk_ref)->chunk->n_filled--;                                                                                       \
    (_chunk_ref)->chunk->particles[(_chunk_ref)->p_index] = (_chunk_ref)->chunk->particles[(_chunk_ref)->chunk->n_filled]; \
    (_chunk_ref)->chunk->n_free++;                                                                                         \
    /* printf("chunk_pop (%d,%d)  n_filled=%d,n_free=%d,p->chunk_state=%d\n", (_chunk_ref)->chunk->xy.x, (_chunk_ref)->chunk->xy.y, (_chunk_ref)->chunk->n_filled, (_chunk_ref)->chunk->n_free, (_chunk_ref)->chunk->particles[(_chunk_ref)->p_index]->chunk_state); */                                                                        \
    (_chunk_ref)->chunk->particles[(_chunk_ref)->chunk->n_filled] = NULL;                                                  \
})

#define box_overlap(_b1, _b2) ((_b1).r >= (_b2).l && (_b1).l <= (_b2).r && (_b1).t >= (_b2).b && (_b1).b <= (_b2).t) 

#define N 50000
#define R 2.0f 
#define WINDOW_WIDTH  1200
#define WINDOW_HEIGHT 1000 


typedef struct GPUParticle {
    float x, y, z; // gpu coords 
    float padding; // vulkan needs 16 byte alignment. maybe theres a fix to this, seems wasteful 
} GPUParticle; 


typedef struct Box {
    float l, r, b, t; 
} Box; 


typedef struct Vec2i {
    uint32_t x, y; 
} Vec2i; 


typedef struct Vec2f {
    float x, y; 
} Vec2f; 


typedef struct ChunkmapInfo {
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


typedef struct ChunkRef {
    Chunk* chunk;
    uint32_t p_index; // particle index in chunk 
} ChunkRef; 


typedef enum ChunkState { 
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
    uint32_t id; 
    float x, y, z; // like world coords  
    float vx, vy; 
    float m; 
}; 


typedef struct Container {
    uint32_t width, height; 
    float zoom, inverse_aspect_ratio, scalar; 
} Container; 


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


void destroy_sdl(
    SDL_GPUDevice* device, 
    SDL_GPUGraphicsPipeline* pipeline, 
    SDL_GPUBuffer* vertex_buffer, 
    SDL_GPUBuffer* index_buffer, 
    SDL_GPUTransferBuffer* live_data_transfer_buffer, 
    SDL_GPUBuffer* live_data_buffer, 
    SDL_GPUGraphicsPipeline* debug_pipeline, 
    SDL_GPUBuffer* vertex_buffer_debug, 
    SDL_GPUBuffer* index_buffer_debug, 
    SDL_Window* window) {

    SDL_ReleaseGPUGraphicsPipeline(device, debug_pipeline);
    SDL_ReleaseGPUBuffer(device, vertex_buffer_debug); 
    SDL_ReleaseGPUBuffer(device, index_buffer_debug); 

    SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    SDL_ReleaseGPUBuffer(device, vertex_buffer); 
    SDL_ReleaseGPUBuffer(device, index_buffer); 
    SDL_ReleaseGPUTransferBuffer(device, live_data_transfer_buffer); 
    SDL_ReleaseGPUBuffer(device, live_data_buffer); 

    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyWindow(window);
    SDL_DestroyGPUDevice(device);
    SDL_Quit();
}


void handle_event(SDL_Event event, bool* quit, uint* sim_state) {
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
        // p->vy -= 1.0f; 
        p->vx += rand_float(-10.0f, 100.0f);  
        p->vy += rand_float(-100.0f, 10.0f);
        float dx = p->vx*dt; 
        float dy = p->vy*dt; 
        p->x += dx;  
        p->y += dy;  

        p->box.l += dx;  
        p->box.r += dx;  
        p->box.b += dy; 
        p->box.t += dy; 

        p->gpu.x += dx*container->scalar;
        p->gpu.y += dy*container->zoom;

        /* printf("particle #%d chunk_state:%d\n", i, p->chunk_state); */
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
                        p->vy *= -1; 
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
                        p->vy *= -1; 
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
                        p->vx *= -1; 
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
                        p->vx *= -1; 
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
                        p->vx *= -1; 
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
                        p->vx *= -1; 
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
                        p->vy *= -1; 
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
                        p->vy *= -1; 
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
        p->x = particle_radius*(1.0f + 2.0f*col); 
        p->y = particle_radius*(1.0f + 2.0f*row); 

        p->box.l = p->x-particle_radius; 
        p->box.r = p->x+particle_radius;
        p->box.b = p->y-particle_radius;
        p->box.t = p->y+particle_radius; 

        p->gpu.x = -1.0f + p->x * container->scalar; 
        p->gpu.y = -1.0f + p->y * container->zoom;
        p->gpu.z = 0.0f; 

        p->vx = rand_float(-v_start, v_start); 
        p->vy = rand_float(-v_start, v_start); 
        /* p->vx = 1000.0f; */  
        /* p->vy = 0.0f; */  

        p->id = i;
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
                        case LRTB: {
                            // nothing to do here 
                        } break; 
                    }
                    /* printf("set particle %d -> chunk<%d,%d>\n", k, i, j); */
                    /* printf("(%d,%d) free: %d\n", i, j, chunk->n_free); */
                    /* p->chunks[p->n_chunks] = chunk; */ 
                    /* p->n_chunks++; */ 
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

    SDL_Window* window; 
    window = SDL_CreateWindow(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_VULKAN);
    if (window == NULL) {
        fprintf(stderr, "ERROR: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;  
    }

    SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, true, NULL); 
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

    SDL_GPUShader* shader_vert = load_shader(device, "shaders/compiled/Circle.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 1, 0); 
    if (shader_vert == NULL) {
        fprintf(stderr, "ERROR: load_shader failed.\n");
        return 1;   
    }

    SDL_GPUShader* shader_frag = load_shader(device, "shaders/compiled/Circle.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 0); 
    if (shader_frag == NULL) {
        fprintf(stderr, "ERROR: load_shader failed.\n");
        return 1;   
    }

    SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {
        .vertex_shader = shader_vert,
        .fragment_shader = shader_frag,
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

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipeline_info);
    if (pipeline == NULL) {
        fprintf(stderr, "ERROR: SDL_CreateGPUGraphicsPipeline failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_ReleaseGPUShader(device, shader_vert); 
    SDL_ReleaseGPUShader(device, shader_frag); 

    SDL_GPUShader* shader_vert_debug = load_shader(device, "shaders/compiled/Line.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 1, 0); 
    if (shader_vert == NULL) {
        fprintf(stderr, "ERROR: load_shader failed.\n");
        return 1;   
    }

    SDL_GPUShader* shader_frag_debug = load_shader(device, "shaders/compiled/SolidColor.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 0); 
    if (shader_frag == NULL) {
        fprintf(stderr, "ERROR: load_shader failed.\n");
        return 1;   
    }

    SDL_GPUGraphicsPipelineCreateInfo debug_pipeline_info = {
        .vertex_shader = shader_vert_debug,
        .fragment_shader = shader_frag_debug,
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
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = (SDL_GPURasterizerState){
            .cull_mode = SDL_GPU_CULLMODE_NONE,
            .fill_mode = SDL_GPU_FILLMODE_LINE,
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
    SDL_GPUGraphicsPipeline* debug_pipeline = SDL_CreateGPUGraphicsPipeline(device, &debug_pipeline_info);
    if (debug_pipeline == NULL) {
        fprintf(stderr, "ERROR: SDL_CreateGPUGraphicsPipeline failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_ReleaseGPUShader(device, shader_vert_debug); 
    SDL_ReleaseGPUShader(device, shader_frag_debug); 

    // ---- [START] vulkan particle setup ----
    SDL_GPUBuffer* vertex_buffer = NULL; 
    SDL_GPUBuffer* index_buffer = NULL; 
    SDL_GPUTransferBuffer* transfer_buffer = NULL; 
    PositionTextureVertex* transfer_data = NULL; 

    uint32_t n_vertices = 4; 
    uint32_t n_indices  = 6; 
    vulkan_buffers_create(device, &vertex_buffer, sizeof(PositionTextureVertex), n_vertices, &index_buffer, n_indices, &transfer_buffer, (void**)&transfer_data);
    
    transfer_data[0] = (PositionTextureVertex) { 
         -1.0f,  1.0f,  0.0f, 
          0.0f,  1.0f, 
         COLOR_TO_UINT8(COLOR_RED),
         COLOR_TO_UINT8(COLOR_TRANSPARENT)
    };
    transfer_data[1] = (PositionTextureVertex) {  
        1.0f,  1.0f,  0.0f, 
        1.0f,  1.0f, 
        COLOR_TO_UINT8(COLOR_GREEN),
        COLOR_TO_UINT8(COLOR_TRANSPARENT)
    };
    transfer_data[2] = (PositionTextureVertex) {  
         1.0f, -1.0f,  0.0f, 
         1.0f,  0.0f, 
        COLOR_TO_UINT8(COLOR_BLUE),
        COLOR_TO_UINT8(COLOR_TRANSPARENT)
    };
    transfer_data[3] = (PositionTextureVertex) { 
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
    for (int i = 0; i < n_vertices; i++) {
        transfer_data[i].x *= container.inverse_aspect_ratio;   
        transfer_data[i].x *= particle_radius*container.zoom;  
        transfer_data[i].y *= particle_radius*container.zoom;  
    }

    Uint16* index_data = (Uint16*) &transfer_data[n_vertices];
    index_data[0] = 2;
    index_data[1] = 1;
    index_data[2] = 0;
    index_data[3] = 2;
    index_data[4] = 0;
    index_data[5] = 3;

    vulkan_buffers_upload(device, vertex_buffer, sizeof(PositionTextureVertex), n_vertices, index_buffer, n_indices, transfer_buffer);

    uint32_t n_particles = N; // Depends on the GPU i guess, I have 8GB VRAM so 1 million should be fine 
    SDL_GPUBuffer* live_data_buffer = SDL_CreateGPUBuffer(
        device,
        &(SDL_GPUBufferCreateInfo) {
            .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
            .size = n_particles * sizeof(GPUParticle)
        }
    );

    SDL_GPUTransferBuffer* live_data_transfer_buffer = SDL_CreateGPUTransferBuffer(
        device,
        &(SDL_GPUTransferBufferCreateInfo) {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = n_particles * sizeof(GPUParticle)
        }
    );
    // ---- [END] vulkan particle setup ----

    // ---- [START] vulkan debug setup ----
    SDL_GPUBuffer* vertex_buffer_debug = NULL; 
    SDL_GPUBuffer* index_buffer_debug = NULL; 
    SDL_GPUTransferBuffer* transfer_buffer_debug = NULL; 
    Vec2Vertex* transfer_data_debug = NULL; 

    uint32_t n_vertices_debug = 4; 
    uint32_t n_indices_debug  = 6; 
    vulkan_buffers_create(device, &vertex_buffer_debug, sizeof(Vec2Vertex), n_vertices_debug, &index_buffer_debug, n_indices_debug, &transfer_buffer_debug, (void**)&transfer_data_debug);
    
    transfer_data_debug[0] = (Vec2Vertex) { 
         -1.0f,  1.0f 
    };
    transfer_data_debug[1] = (Vec2Vertex) {  
        1.0f,  1.0f
    };
    transfer_data_debug[2] = (Vec2Vertex) {  
         1.0f, -1.0f
    };
    transfer_data_debug[3] = (Vec2Vertex) { 
        -1.0f, -1.0f
    };

    /* Container container = { */ 
    /*     .width = WINDOW_WIDTH, */ 
    /*     .height = WINDOW_HEIGHT, */ 
    /*     .zoom = 1/500.0f */ 
    /* }; */ 

    /* container.inverse_aspect_ratio = (float) container.height/container.width; */ 
    /* container.scalar = container.inverse_aspect_ratio * container.zoom; */ 
    /* float particle_radius = R; */ 
    /* for (int i = 0; i < n_vertices; i++) { */
    /*     transfer_data[i].x *= container.inverse_aspect_ratio; */   
    /*     transfer_data[i].x *= particle_radius*container.zoom; */  
    /*     transfer_data[i].y *= particle_radius*container.zoom; */  
    /* } */

    Uint16* index_data_debug = (Uint16*) &transfer_data_debug[n_vertices_debug];
    index_data_debug[0] = 2;
    index_data_debug[1] = 1;
    index_data_debug[2] = 0;
    index_data_debug[3] = 2;
    index_data_debug[4] = 0;
    index_data_debug[5] = 3;

    vulkan_buffers_upload(device, vertex_buffer_debug, sizeof(Vec2Vertex), n_vertices_debug, index_buffer_debug, n_indices_debug, transfer_buffer_debug);
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

    ChunkmapInfo chunkmap_info = (ChunkmapInfo) {.n = (Vec2i) {.x = 20, .y = 16}, .size = (Vec2f) { .x = 0.0f, .y = 0.0f }};
    chunkmap_info.size.x = (float) container.width / chunkmap_info.n.x; 
    chunkmap_info.size.y = (float) container.height / chunkmap_info.n.y; 
    Chunk*** chunkmap = NULL;

    Chunk** border_chunks = NULL;
    uint32_t n_border_chunks = 2 * (chunkmap_info.n.x+chunkmap_info.n.y) - 4; 
    uint32_t n_max_particles_per_chunk = 2 * chunkmap_info.size.x * chunkmap_info.size.y / (particle_radius * particle_radius); 
    printf("chunk_grid: (x:%d,y:%d)\n", chunkmap_info.n.x, chunkmap_info.n.y);
    printf("n_max_particles_per_chunk:%d\n", n_max_particles_per_chunk);

    Particle* particles = NULL; 

    printf("initializing memory...\n");
    if (setup_simulation_memory(&mem_block, &chunkmap, chunkmap_info, &border_chunks, n_border_chunks, n_max_particles_per_chunk, &particles, n_particles) < 0) {
        destroy_sdl(device, pipeline, vertex_buffer, index_buffer, live_data_transfer_buffer, live_data_buffer, debug_pipeline, vertex_buffer_debug, index_buffer_debug, window); 
        return 1; 
    }
    printf("memory initialized successfully!\n");

    if (setup_particles(chunkmap, chunkmap_info, particles, n_particles, particle_radius, &container) < 0) {
        fprintf(stderr, "ERROR: sim setup failed.\n");
        free(mem_block);
        destroy_sdl(device, pipeline, vertex_buffer, index_buffer, live_data_transfer_buffer, live_data_buffer, debug_pipeline, vertex_buffer_debug, index_buffer_debug, window); 
        return 1; 
    }
    printf("%d particles initialized!\n", n_particles);

    uint32_t sim_state = 0; 
    float dt = 0.001f; 

    bool quit = false; 

    while (!quit) {
        SDL_Event event;
        if (SDL_PollEvent(&event)) 
            handle_event(event, &quit, &sim_state); 

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

        GPUParticle* live_data = SDL_MapGPUTransferBuffer(
            device,
            live_data_transfer_buffer,
            true
        );

        // Write to live_data here! 
        for (uint32_t i = 0; i < n_particles; i+=1) {
            live_data[i].x = particles[i].gpu.x;
            live_data[i].y = particles[i].gpu.y;
            live_data[i].z = particles[i].gpu.z; 
        }
        SDL_UnmapGPUTransferBuffer(device, live_data_transfer_buffer); 

        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmdbuf);
        SDL_UploadToGPUBuffer(
            copy_pass,
            &(SDL_GPUTransferBufferLocation) {
                .transfer_buffer = live_data_transfer_buffer,
                .offset = 0
            },
            &(SDL_GPUBufferRegion) {
                .buffer = live_data_buffer,
                .offset = 0,
                .size = sizeof(GPUParticle) * n_particles
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

        SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(cmdbuf, &color_target_info, 1, NULL);
        SDL_BindGPUGraphicsPipeline(render_pass, pipeline);
        SDL_SetGPUViewport(render_pass, &small_viewport);

        SDL_BindGPUVertexBuffers(
            render_pass, 
            0, 
            &(SDL_GPUBufferBinding) {
                .buffer = vertex_buffer, 
                .offset = 0
            }, 
            1
        ); 
        SDL_BindGPUVertexStorageBuffers(
            render_pass,
            0,
            &live_data_buffer,
            1
        );
        SDL_BindGPUIndexBuffer(
            render_pass, 
            &(SDL_GPUBufferBinding) { 
                .buffer = index_buffer, 
                .offset = 0 
            }, 
            SDL_GPU_INDEXELEMENTSIZE_16BIT
        );
        SDL_DrawGPUIndexedPrimitives(render_pass, n_indices, n_particles, 0, 0, 0);

        /* SDL_BindGPUGraphicsPipeline(render_pass, debug_pipeline); */
        /* SDL_SetGPUViewport(render_pass, &small_viewport); */
        /* SDL_BindGPUVertexBuffers( */
        /*     render_pass, */ 
        /*     0, */ 
        /*     &(SDL_GPUBufferBinding) { */
        /*         .buffer = vertex_buffer_debug, */ 
        /*         .offset = 0 */
        /*     }, */ 
        /*     1 */
        /* ); */ 
        /* SDL_BindGPUVertexStorageBuffers( */
        /*     render_pass, */
        /*     0, */
        /*     &live_data_buffer_debug, */
        /*     1 */
        /* ); */
        /* SDL_BindGPUIndexBuffer( */
        /*     render_pass, */ 
        /*     &(SDL_GPUBufferBinding) { */ 
        /*         .buffer = index_buffer_debug, */ 
        /*         .offset = 0 */ 
        /*     }, */ 
        /*     SDL_GPU_INDEXELEMENTSIZE_16BIT */
        /* ); */
        /* SDL_DrawGPUIndexedPrimitives(render_pass, n_indices_debug, n_instances_debug, 0, 0, 0); */

        SDL_EndGPURenderPass(render_pass);

        SDL_SubmitGPUCommandBuffer(cmdbuf);
    }
    
    free(mem_block);
    destroy_sdl(device, pipeline, vertex_buffer, index_buffer, live_data_transfer_buffer, live_data_buffer, debug_pipeline, vertex_buffer_debug, index_buffer_debug, window); 
    return 0; 
}

