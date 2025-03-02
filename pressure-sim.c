#include "pressure-sim-utils.h"
#include <SDL3/SDL_keycode.h>
#include <stdlib.h> 
#include <stdio.h> 
#include <math.h> 

#define vec2_unpack(_vec) ((_vec).x), ((_vec).y)
#define box_unpack(_box) ((_box).l), ((_box).r), ((_box).t), ((_box).b)
#define box_overlap(_b1, _b2) ((_b1).r >= (_b2).l && (_b1).l <= (_b2).r && (_b1).t >= (_b2).b && (_b1).b <= (_b2).t) 
#define new_max(x,y) (((x) >= (y)) ? (x) : (y))

/* #define DEBUG */ 

#define N 50000 
#define R 1.0f 
#define SPEED 1000
#define DT 0.001f 
#define CHUNK_X 30 
#define CHUNK_Y 30
#define WINDOW_WIDTH  1400
#define WINDOW_HEIGHT 1200 


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
} Pipeline_Bomber;

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
    float padd1, padd2; 
} GPUParticle; 


typedef struct {
    float l, r, b, t; 
} Box; 


typedef struct {
    uint32_t x, y; 
} Vec2i; 


typedef struct {
    float x, y; 
} Vec2f; 


typedef struct {
    float x, y, z; 
} Vec3f; 


typedef enum { 
    SIM_INVALID, 
    SIM_RUNNING, 
    SIM_STOPPED, 
    SIM_PAUSED,
    SIM_COUNTER
} SimState;


typedef struct Particle Particle; 
typedef struct Chunk Chunk; 
typedef struct ChunkRef ChunkRef; 

typedef enum ChunkState {
    CS_INVALID, 
    CS_ONE,
    CS_TB,
    CS_LR,
    CS_LRTB,
    CS_COUNTER
} ChunkState; 


static inline const char* chunkstate_to_name(ChunkState cs) {
    static const char *strings[] = { 
		"CS_INVALID", 
		"CS_ONE",
		"CS_TB",
		"CS_LR",
		"CS_LRTB",
		"CS_COUNTER"
  	};  
    return strings[cs];
}



struct ChunkRef {
    Chunk* chunk;
    uint32_t p_index; // particle index in chunk 
}; 


struct Chunk {
    Particle** particles; 
    Chunk* left; 
    Chunk* right; 
    Chunk* bottom; 
    Chunk* top; 
    Box box;
    uint32_t particles_filled; 
    uint32_t particles_free; 
    uint32_t x; 
    uint32_t y; 
}; 


struct Particle {
    ChunkRef chunk_refs[4]; 
    ChunkState chunk_state;
    GPUParticle gpu_pos;
    Box   w_box; 
    Vec2f w_pos; 
    Vec2f w_dpos; 
    Vec2f w_vel; 
    Vec2f w_dvel; 
    float w_mass; 
    float w_rad; 
    uint32_t id; 
}; 



typedef struct {
    Chunk*** chunks; 
    uint32_t chunks_x; 
    uint32_t chunks_y; 
    Vec2f chunks_size; 
    Vec2f dimensions; 
    uint32_t particles_max_per_chunk; 
    Particle* particles; 
    uint32_t particles_n; 
} Chunkmap; 


void particle_print(Particle* p, const char* prefix) { 
    printf("%sp->id:%d\n", prefix, p->id);
    char buf[10000] = ""; 
    char* cur = buf, * const end = buf + sizeof buf; 
    for (uint32_t i = 0; i < 4; i++) {
        if (end <= cur) {
            fprintf(stderr, "particle_print: not enough memory.\n");
            abort(); 
        }
        if (p->chunk_refs[i].chunk) {
            cur += snprintf(cur, end-cur, 
                "\n%s\t%d (%d,%d)@%p p_index=%d,free=%d,filled=%d", 
                prefix, i, p->chunk_refs[i].chunk->x, p->chunk_refs[i].chunk->y, (void*)p->chunk_refs[i].chunk, p->chunk_refs[i].p_index, p->chunk_refs[i].chunk->particles_free, p->chunk_refs[i].chunk->particles_filled);
        }
    }
    printf("%sp->chunk_refs: [%s\n%s]\n", prefix, buf, prefix); 
    printf("%sp->chunk_state:%s\n", prefix, chunkstate_to_name(p->chunk_state));
    printf("%sp->box:%f %f %f %f\n", prefix, box_unpack(p->w_box));
    printf("%sp->gpu:%f %f\n", prefix, vec2_unpack(p->gpu_pos));
    printf("%sp->p:%f %f\n", prefix, vec2_unpack(p->w_pos));
}



bool chunk_ref_is_valid(ChunkRef* chunk_ref) {
    return chunk_ref->chunk->particles_filled > chunk_ref->p_index; 
}


uint32_t chunk_append(Chunk* chunk, Particle* p) {
#ifdef DEBUG
    printf("chunk_append %d,%d@%p\n", chunk->x, chunk->y, (void*)chunk);
    if (chunk->particles_free == 0) {
        fprintf(stderr, "ERROR: appending to full chunk"); 
        abort(); 
    }
#endif // DEBUG
    uint32_t p_index = chunk->particles_filled; 
    chunk->particles[p_index] = p; 
    chunk->particles_filled++;
    chunk->particles_free--; 
    return p_index; 
} 


void chunk_pop(ChunkRef* chunk_ref) {
#ifdef DEBUG 
    printf("chunk_pop (%d,%d) @ %p\n", chunk_ref->chunk->x, chunk_ref->chunk->y, (void*)chunk_ref->chunk);
    if (chunk_ref->chunk->particles_filled == 0) {
        printf("Can't pop empty chunk. exiting.\n"); 
        abort(); 
    }
    if (!chunk_ref_is_valid(chunk_ref)) {
        printf("chunk_pop invalid chunk_ref. exiting.\n"); 
        printf("chunk_pop (%d,%d) @ %p\n", chunk_ref->chunk->x, chunk_ref->chunk->y, (void*)chunk_ref->chunk);
        abort(); 
    }
#endif // DEBUG 
    uint32_t last_index = chunk_ref->chunk->particles_filled - 1; 
    if (last_index != chunk_ref->p_index) {
        bool double_ref = false; 
        for (uint32_t k = 0; k < 4; k++) {
            ChunkRef* other_chunk_ref = &chunk_ref->chunk->particles[last_index]->chunk_refs[k];
            if (other_chunk_ref->chunk && other_chunk_ref->chunk == chunk_ref->chunk && other_chunk_ref->p_index == last_index) {
                if (double_ref) {
                    fprintf(stderr, "Double ref!\n");
                }
                other_chunk_ref->p_index = chunk_ref->p_index;  
                chunk_ref->chunk->particles[chunk_ref->p_index] = chunk_ref->chunk->particles[last_index]; 
                double_ref = true; 
            }
        }

    }
    chunk_ref->chunk->particles[last_index] = NULL; 
    chunk_ref->chunk->particles_filled--; 
    chunk_ref->chunk->particles_free++; 
    chunk_ref->chunk = NULL; 
}


void chunkmap_print(Chunkmap* chunkmap, const char* prefix) {
    printf("-- Chunkmap -- \n"); 
    printf("%schunks@%p\n", prefix, (void*)chunkmap->chunks); 
    printf("%schunks_count:(%d,%d)\n", prefix, chunkmap->chunks_x, chunkmap->chunks_y); 
    printf("%schunks_size:(%f,%f)\n", prefix, vec2_unpack(chunkmap->chunks_size)); 
    printf("%sdimensions:(%f,%f)\n", prefix, vec2_unpack(chunkmap->dimensions)); 
    printf("%sparticles_max_per_chunk:%d\n", prefix, chunkmap->particles_max_per_chunk); 
    printf("%sparticles@%p\n", prefix, (void*)chunkmap->particles); 
    printf("%sparticles_n:%d\n", prefix, chunkmap->particles_n); 
    for (uint32_t i = 0; i < chunkmap->chunks_x; i++) {
        for (uint32_t j = 0; j < chunkmap->chunks_y; j++) {
            if (chunkmap->chunks[i][j]->particles_filled > 0) { 
                printf("%s %d,%d@%p free=%d filled=%d\n", prefix, i, j, (void*)chunkmap->chunks[i][j], chunkmap->chunks[i][j]->particles_free, chunkmap->chunks[i][j]->particles_filled);
            }
        }
    }
    printf("------------- \n"); 
}


typedef struct Stack { 
    ChunkRef stack[10]; 
    uint32_t size; 
    uint32_t capacity; 
} Stack; 


void particle_remove_chunkref(Particle* p, uint32_t i) {
    if (p->chunk_refs[i].chunk != NULL) {
        chunk_pop(&p->chunk_refs[i]);
        p->chunk_refs[i].chunk = NULL; 
    }
}

void particle_set_chunkref(Particle* p, uint32_t i, Chunk* chunk) {
#ifdef DEBUG
    if (chunk == NULL) {
        printf("particle_set_chunk to NULL!\n"); 
        abort(); 
    }
#endif // DEBUG 
    p->chunk_refs[i].chunk = chunk; 
    p->chunk_refs[i].p_index = chunk_append(chunk, p); 
}


void particle_update_chunkref(Particle* p, uint32_t i, Chunk* chunk) {
    if (p->chunk_refs[i].chunk == NULL) {}
    else if (p->chunk_refs[i].chunk != chunk) {
        chunk_pop(&p->chunk_refs[i]);
    } else return; 
    particle_set_chunkref(p, i, chunk); 
}


bool particle_chunkrefs_is_null(Particle* p) {
    for (uint32_t i = 0; i < 4; i++) {
        if (p->chunk_refs[i].chunk != NULL) {
            return false;
        } 
    }
    return true;
} 


void particle_set_chunk_state_one(Particle* p, Chunk* chunk_one) {
#ifdef DEBUG
    printf("particle_set_chunk_state_one\n");
#endif // DEBUG
    switch (p->chunk_state) {
    case CS_ONE: {
        particle_remove_chunkref(p, 0);
#ifdef DEBUG
        if (!particle_chunkrefs_is_null(p)) {
            printf("not null\n");
            particle_print(p, "CS_ONE "); 
            abort();
        }
#endif // DEBUG 
        particle_update_chunkref(p, 0, chunk_one);
    } break; 
    case CS_TB: {
        particle_remove_chunkref(p, 2); 
        particle_remove_chunkref(p, 3); 
#ifdef DEBUG
        if (!particle_chunkrefs_is_null(p)) {
            printf("not null\n");
            particle_print(p, "CS_TB "); 
            abort();
        }
#endif // DEBUG 
        particle_update_chunkref(p, 0, chunk_one);
        p->chunk_state = CS_ONE;     
    } break; 
    case CS_LR: {
        particle_remove_chunkref(p, 0);
        particle_remove_chunkref(p, 1); 
#ifdef DEBUG
        if (!particle_chunkrefs_is_null(p)) {
            printf("not null\n");
            particle_print(p, "CS_LR "); 
            abort();
        }
#endif // DEBUG 
        particle_update_chunkref(p, 0, chunk_one);
        p->chunk_state = CS_ONE;     
    } break; 
    case CS_LRTB: {
        particle_remove_chunkref(p, 0);
        particle_remove_chunkref(p, 1); 
        particle_remove_chunkref(p, 2); 
        particle_remove_chunkref(p, 3); 
#ifdef DEBUG
        if (!particle_chunkrefs_is_null(p)) {
            printf("not null\n");
            particle_print(p, ""); 
            abort();
        }
#endif // DEBUG 
        particle_update_chunkref(p, 0, chunk_one);
        p->chunk_state = CS_ONE;     
    } break; 
    default: {
        fprintf(stderr, "invalid chunk state\n"); 
        abort(); 
    } break; 
    }; 
#ifdef DEBUG
    if (p->chunk_refs[0].chunk == NULL || p->chunk_refs[1].chunk != NULL || p->chunk_refs[2].chunk != NULL || p->chunk_refs[3].chunk != NULL) {
        particle_print(p, "?? "); 
        abort(); 
    }
#endif // DEBUG
}

void particle_set_chunk_state_lr(Particle* p, Chunk* chunk_left, Chunk* chunk_right) {
#ifdef DEBUG
    printf("particle_set_chunk_state_lr\n");
#endif // DEBUG
    switch (p->chunk_state) {
    case CS_ONE: {
        particle_remove_chunkref(p, 0);
#ifdef DEBUG
        if (!particle_chunkrefs_is_null(p)) {
            printf("not null\n");
            particle_print(p, ""); 
            abort();
        }
#endif // DEBUG 
        particle_update_chunkref(p, 0, chunk_left);
        particle_update_chunkref(p, 1, chunk_right);
        p->chunk_state = CS_LR;     
    } break; 
    case CS_TB: {
        particle_remove_chunkref(p, 2); 
        particle_remove_chunkref(p, 3); 
#ifdef DEBUG
        if (!particle_chunkrefs_is_null(p)) {
            printf("not null\n");
            particle_print(p, ""); 
            abort();
        }
#endif // DEBUG 
        particle_update_chunkref(p, 0, chunk_left);
        particle_update_chunkref(p, 1, chunk_right);
        p->chunk_state = CS_LR;     
    } break; 
    case CS_LR: {
        particle_remove_chunkref(p, 0); 
        particle_remove_chunkref(p, 1); 
#ifdef DEBUG
        if (!particle_chunkrefs_is_null(p)) {
            printf("not null\n");
            particle_print(p, ""); 
            abort();
        }
#endif // DEBUG 
        particle_set_chunkref(p, 0, chunk_left);
        particle_set_chunkref(p, 1, chunk_right);
    } break; 
    case CS_LRTB: {
        particle_remove_chunkref(p, 0); 
        particle_remove_chunkref(p, 1); 
        particle_remove_chunkref(p, 2); 
        particle_remove_chunkref(p, 3); 
#ifdef DEBUG
        if (!particle_chunkrefs_is_null(p)) {
            printf("not null\n");
            particle_print(p, ""); 
            abort();
        }
#endif // DEBUG 
        particle_set_chunkref(p, 0, chunk_left);
        particle_set_chunkref(p, 1, chunk_right);
        p->chunk_state = CS_LR;     
    } break; 
    default: {
        fprintf(stderr, "invalid chunk state\n"); 
        abort(); 
    } break; 
    }; 
    if (p->chunk_refs[0].chunk == NULL || p->chunk_refs[1].chunk == NULL || p->chunk_refs[2].chunk != NULL || p->chunk_refs[3].chunk != NULL) {
        particle_print(p, "?? "); 
        abort(); 
    }
}

void particle_set_chunk_state_tb(Particle* p, Chunk* chunk_top, Chunk* chunk_bottom) {
#ifdef DEBUG
    printf("particle_set_chunk_state_tb\n");
    if (p == NULL) {
        printf("p == NULL!\n"); 
        abort(); 
    }
    if (chunk_top == NULL) {
        printf("chunk_top == NULL!\n"); 
        abort(); 
    }
    if (chunk_bottom == NULL) {
        printf("chunk_bottom == NULL!\n"); 
        abort(); 
    }
#endif // DEBUG
    switch (p->chunk_state) {
    case CS_ONE: {
        particle_remove_chunkref(p, 0); 
#ifdef DEBUG
        if (!particle_chunkrefs_is_null(p)) {
            printf("not null\n");
            particle_print(p, ""); 
            abort();
        }
#endif // DEBUG 
        particle_set_chunkref(p, 2, chunk_top);
        particle_set_chunkref(p, 3, chunk_bottom);
        p->chunk_state = CS_TB;     
    } break; 
    case CS_TB: {
        particle_remove_chunkref(p, 2); 
        particle_remove_chunkref(p, 3); 
#ifdef DEBUG
        if (!particle_chunkrefs_is_null(p)) {
            printf("not null\n");
            particle_print(p, ""); 
            abort();
        }
#endif // DEBUG 
        particle_set_chunkref(p, 2, chunk_top);
        particle_set_chunkref(p, 3, chunk_bottom);
    } break; 
    case CS_LR: {
        particle_remove_chunkref(p, 0); 
        particle_remove_chunkref(p, 1); 
#ifdef DEBUG
        if (!particle_chunkrefs_is_null(p)) {
            printf("not null\n");
            particle_print(p, ""); 
            abort();
        }
#endif // DEBUG 
        particle_set_chunkref(p, 2, chunk_top);
        particle_set_chunkref(p, 3, chunk_bottom);
        p->chunk_state = CS_TB;     
    } break; 
    case CS_LRTB: {
        particle_remove_chunkref(p, 0); 
        particle_remove_chunkref(p, 1); 
        particle_remove_chunkref(p, 2); 
        particle_remove_chunkref(p, 3); 
#ifdef DEBUG
        if (!particle_chunkrefs_is_null(p)) {
            printf("not null\n");
            particle_print(p, ""); 
            abort();
        }
#endif // DEBUG 
        particle_set_chunkref(p, 2, chunk_top);
        particle_set_chunkref(p, 3, chunk_bottom);
        p->chunk_state = CS_TB;     
    } break; 
    default: {
        fprintf(stderr, "invalid chunk state\n"); 
        abort(); 
    } break; 
    }; 
    if (p->chunk_refs[0].chunk != NULL || p->chunk_refs[1].chunk != NULL || p->chunk_refs[2].chunk == NULL || p->chunk_refs[3].chunk == NULL) {
        particle_print(p, "?? "); 
        abort(); 
    }
}

void particle_set_chunk_state_lrtb(Particle* p, Chunk* chunk_bottom_right, Chunk* chunk_top_right, Chunk* chunk_top_left, Chunk* chunk_bottom_left) {
#ifdef DEBUG
    printf("particle_set_chunk_state_lrtb\n");
#endif // DEBUG
    switch (p->chunk_state) {
    case CS_ONE: {
        particle_remove_chunkref(p, 0); 
#ifdef DEBUG
        if (!particle_chunkrefs_is_null(p)) {
            printf("not null\n");
            particle_print(p, "CS_ONE "); 
            abort();
        }
#endif // DEBUG
        particle_set_chunkref(p, 0, chunk_bottom_right);
        particle_set_chunkref(p, 1, chunk_top_right);
        particle_set_chunkref(p, 2, chunk_top_left);
        particle_set_chunkref(p, 3, chunk_bottom_left);
        p->chunk_state = CS_LRTB;     
    } break; 
    case CS_TB: {
        particle_remove_chunkref(p, 2); 
        particle_remove_chunkref(p, 3); 
#ifdef DEBUG
        if (!particle_chunkrefs_is_null(p)) {
            printf("not null\n");
            particle_print(p, "CS_TB "); 
            abort();
        }
#endif // DEBUG
        particle_set_chunkref(p, 0, chunk_bottom_right);
        particle_set_chunkref(p, 1, chunk_top_right);
        particle_set_chunkref(p, 2, chunk_top_left); 
        particle_set_chunkref(p, 3, chunk_bottom_left); 
        p->chunk_state = CS_LRTB;     
    } break; 
    case CS_LR: {
        particle_remove_chunkref(p, 0); 
        particle_remove_chunkref(p, 1); 
#ifdef DEBUG
        if (!particle_chunkrefs_is_null(p)) {
            printf("not null\n");
            particle_print(p, "CS_LR "); 
            abort();
        }
#endif // DEBUG
        particle_set_chunkref(p, 0, chunk_bottom_right);
        particle_set_chunkref(p, 1, chunk_top_right);
        particle_set_chunkref(p, 2, chunk_top_left); 
        particle_set_chunkref(p, 3, chunk_bottom_left); 
        p->chunk_state = CS_LRTB;     
    } break; 
    case CS_LRTB: {
        particle_remove_chunkref(p, 0); 
        particle_remove_chunkref(p, 1); 
        particle_remove_chunkref(p, 2); 
        particle_remove_chunkref(p, 3); 
#ifdef DEBUG
        if (!particle_chunkrefs_is_null(p)) {
            printf("not null\n");
            particle_print(p, "CS_LRTB "); 
            abort();
        }
#endif 
        particle_set_chunkref(p, 0, chunk_bottom_right);
        particle_set_chunkref(p, 1, chunk_top_right);
        particle_set_chunkref(p, 2, chunk_top_left); 
        particle_set_chunkref(p, 3, chunk_bottom_left); 
    } break; 
    default: {
        fprintf(stderr, "invalid chunk state\n"); 
        abort(); 
    } break; 
    }; 
    if (p->chunk_refs[0].chunk == NULL || p->chunk_refs[1].chunk == NULL || p->chunk_refs[2].chunk == NULL || p->chunk_refs[3].chunk == NULL) {
        particle_print(p, "?? "); 
        abort(); 
    }
}


void collide(Particle* p1, Particle* p2) {
#ifdef DEBUG
    if (p1 == NULL) {
        fprintf(stderr, "p1 is NULL\n"); 
        abort(); 
    } 
    if (p2 == NULL) {
        fprintf(stderr, "p2 is NULL\n"); 
        abort(); 
    } 
#endif // DEBUG 
    float dx = p1->w_pos.x - p2->w_pos.x;
    float dy = p1->w_pos.y - p2->w_pos.y;
    float dr = p1->w_rad + p2->w_rad; 
    float inv_sqrt = 1.0f/sqrt(dx*dx + dy*dy);
    /* printf("%f, %f\n", vec2_unpack(p1->p)); */
    /* printf("%f, %f\n", vec2_unpack(p2->p)); */
    if (dx*dx + dy*dy <= dr*dr*1.000f) {
        Vec2f tmp = p1->w_vel; 
        p1->w_vel = p2->w_vel; 
        p2->w_vel = tmp; 
        /* printf("Collision %f\n", dr); */
        float alpha = 1.0f*(dr*inv_sqrt-1.0f);
        alpha *= 1.1f; 
        p1->w_dpos.x += alpha*dx;  
        p1->w_dpos.y += alpha*dy;  
        /* p2->w_dpos.x += -alpha*dx; */  
        /* p2->w_dpos.y += -alpha*dy; */  
        /* p1->w_pos.x = 0; */ 
        /* p1->w_pos.y = 0; */ 
        /* p1->w_box.l = 0; */  
        /* p1->w_box.r = p1->w_rad*2; */  
        /* p1->w_box.b = 0; */ 
        /* p1->w_box.t = p1->w_rad*2; */ 
        /* p2->w_pos.x = 0; */ 
        /* p2->w_pos.y = 0; */ 
        /* p2->w_box.l = 0; */  
        /* p2->w_box.r = p2->w_rad*2; */  
        /* p2->w_box.b = 0; */ 
        /* p2->w_box.t = p2->w_rad*2; */ 
    }
}


void particle_collisions(Particle* p, ChunkRef chunk_ref) {
    for (uint32_t i = 0; i < chunk_ref.p_index; i++) {
        collide(p, chunk_ref.chunk->particles[i]);
    }
    for (uint32_t i = chunk_ref.p_index+1; i < chunk_ref.chunk->particles_filled; i++) {
        collide(p, chunk_ref.chunk->particles[i]);
    }
}


void destroy_sdl(
    SDL_GPUDevice* device, 
    SDL_Window* window,
    Pipeline_Bomber* destroyers,
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


void event_handle(SDL_Event event, bool* quit, bool* debug_mode, SimState* sim_state, uint32_t* steps, float* dt) {
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
            *steps += 1;
        } break; 
        case SDLK_D: {
            *debug_mode = !*debug_mode; 
        } break; 
        case SDLK_SPACE: {
            switch(*sim_state) {
                case SIM_RUNNING: { 
                    *sim_state = SIM_PAUSED;
                    printf("Simulation: paused!\n");
                } break; 
                case SIM_PAUSED: { 
                    *sim_state = SIM_RUNNING;
                    printf("Simulation: running!\n");
                } break; 
                default: {} break; 
            }
        } break;
        case SDLK_LEFTBRACKET: {
            *dt -= DT * 0.1f; 
            printf("dt=%f\n", *dt); 
        } break; 
        case SDLK_RIGHTBRACKET: {
            *dt += DT * 0.1f; 
            printf("dt=%f\n", *dt); 
        } break; 
        }
    } break; 
    } 
}



// FIXME: Redo chunk tracking, it's bad
// - ChunkState okay 
// - Use binary search to account for big jumps 
// - split the grid into groups of chunks to search
// 
// other option (worse, but easier to implement): 
// - rerun chunk_overlap over groups of chunks 
int physics_tick(float dt, Chunkmap* chunkmap, float particle_radius, Container* container) {
    for (Particle* p = chunkmap->particles; p < chunkmap->particles + chunkmap->particles_n; p++) {
        uint32_t i = UINT32_MAX, j = UINT32_MAX;
        bool lambda_cond = false, mu_cond = false;
        float border_pad = 0.1f; 
        if (p->w_box.l <= 0.0f) { 
            p->w_vel.x *= -1.0f; 
            p->w_pos.x = 0.0f + particle_radius + border_pad; 
            p->w_box.l = border_pad;  
            p->w_box.r = 2 * particle_radius + border_pad;  
            lambda_cond = true; 
            i = 0; 
        } else if (p->w_box.r >= chunkmap->dimensions.x) {
            p->w_vel.x *= -1.0f; 
            p->w_pos.x = chunkmap->dimensions.x - particle_radius - border_pad; 
            p->w_box.l = p->w_pos.x - particle_radius - border_pad;
            p->w_box.r = chunkmap->dimensions.x - border_pad;
            lambda_cond = true; 
            i = chunkmap->chunks_x - 1; 
        }
        if (p->w_box.b <= 0.0f) {
            p->w_vel.y *= -1.0f; 
            p->w_pos.y = 0.0f + particle_radius + border_pad; 
            p->w_box.b = 0.0f + border_pad;  
            p->w_box.t = 2 * particle_radius + border_pad;  
            mu_cond = true; 
            j = 0; 
        } else if (p->w_box.t >= chunkmap->dimensions.y) {
            p->w_vel.y *= -1.0f; 
            p->w_pos.y = chunkmap->dimensions.y - particle_radius - border_pad; 
            p->w_box.b = p->w_pos.y - particle_radius - border_pad;
            p->w_box.t = chunkmap->dimensions.y - border_pad;
            mu_cond = true; 
            j = chunkmap->chunks_y - 1; 
        }
        
        if (!lambda_cond) {
            float lambda = p->w_box.l/chunkmap->chunks_size.x; 
            uint32_t lambda_floor = floorf(lambda); 
            /* lambda_cond = lambda > lambda_floor && lambda < lambda_floor + 1 - 2 * particle_radius; */
            lambda_cond = lambda > lambda_floor && lambda + 2*particle_radius/chunkmap->chunks_size.x < lambda_floor+1; 
            i = lambda_floor; 
        }
        if (!mu_cond) {
            float mu = p->w_box.b/chunkmap->chunks_size.y; 
            uint32_t mu_floor = floorf(mu); 
            // mu_cond = mu > mu_floor && mu < mu_floor + 1 - 2 * particle_radius;
            mu_cond = mu > mu_floor && mu + 2*particle_radius/chunkmap->chunks_size.y < mu_floor+1; 
            j = mu_floor; 
        }

        if (lambda_cond && mu_cond) { // ONE
            Chunk* chunk_one = chunkmap->chunks[i][j]; 
            particle_set_chunk_state_one(p, chunk_one);  
        } else if (lambda_cond && !mu_cond) { // TOP_BOTTOM 
            Chunk* chunk_bottom = chunkmap->chunks[i][j]; 
            Chunk* chunk_top = chunk_bottom->top;  
            particle_set_chunk_state_tb(p, chunk_top, chunk_bottom); 
        } else if (!lambda_cond && mu_cond) { // LEFT_RIGHT 
            Chunk* chunk_left = chunkmap->chunks[i][j]; 
            Chunk* chunk_right = chunk_left->right;  
            particle_set_chunk_state_lr(p, chunk_left, chunk_right); 
        } else if (!lambda_cond && !mu_cond) { // LRTB 
            Chunk* chunk_bottom_left = chunkmap->chunks[i][j]; 
            Chunk* chunk_bottom_right = chunk_bottom_left->right; 
            Chunk* chunk_top_left = chunk_bottom_left->top;  
            Chunk* chunk_top_right = chunk_bottom_right->top;  
            particle_set_chunk_state_lrtb(p, chunk_bottom_right, chunk_top_right, chunk_top_left, chunk_bottom_left); 
        }

        float dx = p->w_vel.x*dt; 
        float dy = p->w_vel.y*dt; 
        p->w_dpos.x = dx; 
        p->w_dpos.y = dy; 

        switch(p->chunk_state) {
        case CS_ONE: {
            particle_collisions(p, p->chunk_refs[0]);     
        } break; 
        case CS_LR: {
            particle_collisions(p, p->chunk_refs[0]);     
            particle_collisions(p, p->chunk_refs[1]);     
        } break; 
        case CS_TB: {
            particle_collisions(p, p->chunk_refs[2]);     
            particle_collisions(p, p->chunk_refs[3]);     
        } break; 
        case CS_LRTB: {
            particle_collisions(p, p->chunk_refs[0]);     
            particle_collisions(p, p->chunk_refs[1]);     
            particle_collisions(p, p->chunk_refs[2]);     
            particle_collisions(p, p->chunk_refs[3]);     
        } break; 
        default: {
            fprintf(stderr, "invalid chunk state\n");
        } break; 
        }
        p->w_pos.x += p->w_dpos.x;  
        p->w_pos.y += p->w_dpos.y;  

        p->w_box.l += p->w_dpos.x;  
        p->w_box.r += p->w_dpos.x;  
        p->w_box.b += p->w_dpos.y; 
        p->w_box.t += p->w_dpos.y; 

        p->gpu_pos.x += p->w_dpos.x*container->scalar;
        p->gpu_pos.y += p->w_dpos.y*container->zoom;
    }
    return 0;
}


int setup_particles(Chunkmap* chunkmap, float particle_radius, Container* container) {
    float pad = 1.0f * particle_radius; 
    uint32_t particles_per_row = 1.0f/((particle_radius + pad)*container->scalar);
    uint32_t particles_per_col = 1.0f/((particle_radius + pad)*container->zoom);

    uint32_t particles_n_max = particles_per_row*particles_per_col;
    if (chunkmap->particles_n > particles_n_max) {
        fprintf(stderr, "Too many particles %d for container %d\n", chunkmap->particles_n, particles_n_max); 
        return -1; 
    }

    float v_start = SPEED;  
    for (uint32_t i = 0; i < chunkmap->particles_n; i++) { 
        // Particle* p = &((Particle*)chunkmap->particles)[i]; 
        Particle* p = &chunkmap->particles[i];

        uint32_t col = i%particles_per_row;
        uint32_t row = (uint32_t) (i/particles_per_row);
        p->w_pos.x = (particle_radius + pad)*(1.0f + 2.0f*col); 
        p->w_pos.y = (particle_radius + pad)*(1.0f + 2.0f*row); 

        p->w_box.l = p->w_pos.x-particle_radius; 
        p->w_box.r = p->w_pos.x+particle_radius;
        p->w_box.b = p->w_pos.y-particle_radius;
        p->w_box.t = p->w_pos.y+particle_radius; 

        p->gpu_pos.x = -1.0f + p->w_pos.x * container->scalar; 
        p->gpu_pos.y = -1.0f + p->w_pos.y * container->zoom;

        p->w_vel.x = rand_float(-v_start, v_start); 
        p->w_vel.y = rand_float(-v_start, v_start); 

        /* p->v.x = -SPEED; */  
        /* p->v.y = -SPEED; */ 
        // particle_print(p); 
        p->id = i; 
        p->w_rad = particle_radius;
    }

    for (uint32_t i = 0; i < chunkmap->chunks_x; i++) {
        for (uint32_t j = 0; j < chunkmap->chunks_y; j++) {
            Chunk* chunk = chunkmap->chunks[i][j]; 
            for (uint32_t k = 0; k < chunkmap->particles_n; k++) {
                Particle* p = &chunkmap->particles[k]; 
                /* particle_print(p, "\t\t\t"); */
                if (box_overlap(p->w_box, chunk->box)) {
                    switch (p->chunk_state) {
                        case CS_INVALID: {
                            particle_set_chunkref(p, 0, chunk);
                            p->chunk_state = CS_ONE; 
                        } break; 
                        case CS_ONE: {
                            if (p->chunk_refs[0].chunk->right == chunk) { // the way we iterate, we only have to check if its a chunk to the right  
                                particle_set_chunkref(p, 1, chunk); 
                                p->chunk_state = CS_LR; 
                            } else if (p->chunk_refs[0].chunk->top == chunk) {
                                Chunk* chunk_bottom = p->chunk_refs[0].chunk;
                                particle_remove_chunkref(p, 0); 
                                particle_set_chunkref(p, 2, chunk); 
                                particle_set_chunkref(p, 3, chunk_bottom); 
                                p->chunk_state = CS_TB; 
                            }
                        } break; 
                        case CS_TB: { 
                            Chunk* chunk_top_right = p->chunk_refs[2].chunk->right; 
                            Chunk* chunk_bottom_right = p->chunk_refs[3].chunk->right; 
                            particle_set_chunkref(p, 0, chunk_bottom_right);
                            particle_set_chunkref(p, 1, chunk_top_right);
                            p->chunk_state = CS_LRTB; 
                        } break; 
                        case CS_LR: { 
                            Chunk* chunk_top_left = p->chunk_refs[0].chunk->top; 
                            Chunk* chunk_top_right = p->chunk_refs[1].chunk->top; 
                            Chunk* chunk_bottom_right = p->chunk_refs[1].chunk; 
                            Chunk* chunk_bottom_left = p->chunk_refs[0].chunk; 
                            particle_remove_chunkref(p, 0); 
                            particle_remove_chunkref(p, 1); 
                            particle_set_chunkref(p, 0, chunk_bottom_right);
                            particle_set_chunkref(p, 1, chunk_top_right);
                            particle_set_chunkref(p, 2, chunk_top_left);
                            particle_set_chunkref(p, 3, chunk_bottom_left);
                            p->chunk_state = CS_LRTB; 
                        } break; 
                        case CS_LRTB: { // nothing to do here 
                        } break; 
                        default: {
                            fprintf(stderr, "ERROR: Invalid chunk state\n");
                            return -1; 
                        } break; 
                    }
                } else {
                }
            }
        } 
    } 
    return 0; 
}


void setup_chunk(Chunkmap* chunkmap, uint32_t i, uint32_t j) {
    Chunk* chunk = chunkmap->chunks[i][j]; 
    chunk->particles = (Particle**)((char*)chunk + sizeof *chunk); 
    memset(chunk->particles, 0, chunkmap->particles_max_per_chunk * sizeof chunk->particles[0]);
    chunk->box.l = i*chunkmap->chunks_size.x; 
    chunk->box.r = (i+1)*chunkmap->chunks_size.x;
    chunk->box.b = j*chunkmap->chunks_size.y;
    chunk->box.t = (j+1)*chunkmap->chunks_size.y;
    chunk->particles_filled = 0; 
    chunk->particles_free = chunkmap->particles_max_per_chunk;
    chunk->x = i; 
    chunk->y = j; 
}


int setup_simulation_memory(void** mem_block_ptr, Chunkmap* chunkmap) {
    uint32_t nx = chunkmap->chunks_x; 
    uint32_t ny = chunkmap->chunks_y; 
    size_t total_size = 
        nx * sizeof chunkmap->chunks[0] +
        nx * ny * sizeof chunkmap->chunks[0][0] +
        nx * ny * sizeof *chunkmap->chunks[0][0] + 
        nx * ny * chunkmap->particles_max_per_chunk * sizeof chunkmap->chunks[0][0]->particles[0] +
        chunkmap->particles_n * sizeof *chunkmap->chunks[0][0]->particles[0];  

    char* mem_block = (void*)malloc(total_size);
    if (mem_block == NULL) {
        fprintf(stderr, "ERROR: malloc of memory block (size=%zu) failed.\n", total_size);
        return -1;
    }
    printf("Allocated %zu bytes on heap.\n", total_size);
    *mem_block_ptr = mem_block;
    Chunk*** chunks = (Chunk***)mem_block; 
    chunkmap->chunks = chunks; 
    chunks[0] = (Chunk**)((char*)chunks + nx * sizeof chunkmap->chunks[0]); 
    chunks[0][0] = (Chunk*)((char*)chunks[0] + nx * ny * sizeof chunks[0]); 
    setup_chunk(chunkmap, 0, 0); 
    for (uint32_t i = 1; i < nx; i++) {
        chunks[i] = (Chunk**)((char*)chunks[i-1] + ny * sizeof chunks[0]); 
        chunks[i][0] = (Chunk*)((char*)chunks[i-1][0] + ny * (sizeof *chunks[0][0] + chunkmap->particles_max_per_chunk * sizeof chunks[0][0]->particles[0]));
        setup_chunk(chunkmap, i, 0); // 1,0 2,0 3,0  
    }
    for (uint32_t i = 0; i < nx; i++) {
        for (uint32_t j = 1; j < ny; j++) {
            chunks[i][j] = (Chunk*)((char*)chunks[i][j-1] + sizeof *chunks[0][0] + chunkmap->particles_max_per_chunk * sizeof chunks[0][0]->particles[0]); 
            setup_chunk(chunkmap, i, j); // 0,1 0,2 0,3 ... 1,1 1,2,1,3 ... 2,1 
        }
    }
    chunkmap->particles = (Particle*) ((char*)chunks[nx-1][ny-1] + sizeof *chunks[0][0] + chunkmap->particles_max_per_chunk * sizeof chunks[0][0]->particles[0]); 
    for (uint32_t i = 0; i < chunkmap->chunks_x; i++) {
        for (uint32_t j = 0; j < chunkmap->chunks_y; j++) {
            chunkmap->chunks[i][j]->left = i == 0 ? NULL : chunkmap->chunks[i-1][j]; 
            chunkmap->chunks[i][j]->right = i == chunkmap->chunks_x-1 ? NULL : chunkmap->chunks[i+1][j]; 
            chunkmap->chunks[i][j]->bottom = j == 0 ? NULL : chunkmap->chunks[i][j-1]; 
            chunkmap->chunks[i][j]->top = j == chunkmap->chunks_y-1 ? NULL : chunkmap->chunks[i][j+1]; 
        }

    }
    return 0; 
}


int main(int argc, char* argv[]) {
    srand(0); 
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

    printf("OK: Created device with driver '%s'\n", SDL_GetGPUDeviceDriver(device));
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

    float particle_radius = R; 

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

    Chunkmap chunkmap = { 0 };  
    chunkmap.chunks_x = CHUNK_X; 
    chunkmap.chunks_y = CHUNK_Y; 
    chunkmap.chunks_size.x = (float) container.width / chunkmap.chunks_x; 
    chunkmap.chunks_size.y = (float) container.height / chunkmap.chunks_y; 
    chunkmap.dimensions.x = (float) container.width;
    chunkmap.dimensions.y = (float) container.height;
    chunkmap.particles_max_per_chunk = new_max(2 * chunkmap.chunks_size.x * chunkmap.chunks_size.y / (particle_radius * particle_radius), 100); 
    chunkmap.particles_n = N; 

    SDL_GPUBuffer* particles_sso_buffer = SDL_CreateGPUBuffer(
        device,
        &(SDL_GPUBufferCreateInfo) {
            .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
            .size = chunkmap.particles_n * sizeof(GPUParticle)
        }
    );

    SDL_GPUTransferBuffer* particles_sso_transfer_buffer = SDL_CreateGPUTransferBuffer(
        device,
        &(SDL_GPUTransferBufferCreateInfo) {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = chunkmap.particles_n * sizeof(GPUParticle)
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

    uint32_t n_lines = chunkmap.chunks_x + chunkmap.chunks_y - 2; 
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


    printf("initializing memory...\n");
    Pipeline_Bomber destroyers[] = {
        (Pipeline_Bomber){
            .pipeline = particles_pipeline, 
            .vertex_buffer = particles_vertex_buffer, 
            .index_buffer = particles_index_buffer, 
            .sso_buffer = particles_sso_buffer, 
            .sso_transfer_buffer = particles_sso_transfer_buffer
        },
        (Pipeline_Bomber){
            .pipeline = debug_lines_pipeline, 
            .vertex_buffer = debug_lines_vertex_buffer, 
            .index_buffer = debug_lines_index_buffer, 
            .sso_buffer = debug_lines_sso_buffer, 
            .sso_transfer_buffer = debug_lines_sso_transfer_buffer
        },
    }; 
    if (setup_simulation_memory(&mem_block, &chunkmap) < 0) {
        destroy_sdl(device, window, destroyers, 2, debug_pipeline_maskee, texture_depth_stencil);  
        return 1; 
    }
    printf("memory initialized successfully!\n");
    chunkmap_print(&chunkmap, "");


    printf("setting up particles...\n");
    if (setup_particles(&chunkmap, particle_radius, &container) < 0) {
        fprintf(stderr, "ERROR: sim setup failed.\n");
        free(mem_block);
        destroy_sdl(device, window, destroyers, 2, debug_pipeline_maskee, texture_depth_stencil);  
        return 1; 
    }
    printf("%d particles initialized!\n", chunkmap.particles_n);

    SimState sim_state = SIM_PAUSED; 
    float dt = DT;  

    bool quit = false; 
    bool debug_mode = false; 
    uint32_t steps = 0; 

    while (!quit) {
        SDL_Event event;
        if (SDL_PollEvent(&event)) 
            event_handle(event, &quit, &debug_mode, &sim_state, &steps, &dt); 

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

        switch (sim_state) {
            case SIM_RUNNING: {
                if (physics_tick(dt, &chunkmap, particle_radius, &container) < 0) {
                    fprintf(stderr, "ERROR: physics_tick, stopping simulation.\n"); 
                    sim_state = SIM_STOPPED; 
                }  
            } break; 
            case SIM_PAUSED: {
                while (steps > 0) {
                    printf("Stepping 1\n");
                    if (physics_tick(dt, &chunkmap, particle_radius, &container) < 0) {
                        fprintf(stderr, "ERROR: physics_tick, stopping simulation.\n"); 
                        sim_state = SIM_STOPPED; 
                    }  
                    steps--; 
                }
            } break; 
            case SIM_STOPPED: {

            } break; 
            default: {
                fprintf(stderr, "Sim state invalid.\n"); 
            } break; 
        }
        GPUParticle* particles_sso_data = SDL_MapGPUTransferBuffer(device, particles_sso_transfer_buffer, true);
        for (uint32_t i = 0; i < chunkmap.particles_n; i+=1) {
            particles_sso_data[i].x = chunkmap.particles[i].gpu_pos.x;
            particles_sso_data[i].y = chunkmap.particles[i].gpu_pos.y;
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
                .size = sizeof(GPUParticle) * chunkmap.particles_n 
            },
            false   
        );
        SDL_EndGPUCopyPass(copy_pass);


        GPULine* debug_lines_data = SDL_MapGPUTransferBuffer(device, debug_lines_sso_transfer_buffer, true);
        for (uint32_t i = 0; i < chunkmap.chunks_x - 1; i+=1) { // vertical 
            debug_lines_data[i].x = 2 * (float)(i + 1) / (chunkmap.chunks_x); 
            debug_lines_data[i].flags = 0; 
        }
        for (uint32_t i = chunkmap.chunks_x - 1; i < n_lines; i+=1) { // horizontal 
            uint32_t index = i - chunkmap.chunks_x + 1; 
            debug_lines_data[i].y = 2 * (float)(index + 1) / (chunkmap.chunks_y); 
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

        /* SDL_GPUDepthStencilTargetInfo depth_stencil_target_info = { 0 }; */
        /* depth_stencil_target_info.texture           = texture_depth_stencil; */
        /* depth_stencil_target_info.clear_depth       = 0.0f; */
        /* depth_stencil_target_info.load_op           = SDL_GPU_LOADOP_CLEAR; */
        /* depth_stencil_target_info.store_op          = SDL_GPU_STOREOP_DONT_CARE; */
        /* depth_stencil_target_info.stencil_load_op   = SDL_GPU_LOADOP_CLEAR; */
        /* depth_stencil_target_info.stencil_store_op  = SDL_GPU_STOREOP_DONT_CARE; */
        /* depth_stencil_target_info.cycle             = true; */
        /* depth_stencil_target_info.clear_stencil     = 0; */

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
            SDL_DrawGPUIndexedPrimitives(render_pass, particles_n_indices, chunkmap.particles_n, 0, 0, 0);
        }

        SDL_EndGPURenderPass(render_pass);
        SDL_SubmitGPUCommandBuffer(cmdbuf);
    }
    
    free(mem_block);
    destroy_sdl(device, window, destroyers, 2, debug_pipeline_maskee, texture_depth_stencil);  
    return 0; 
}

