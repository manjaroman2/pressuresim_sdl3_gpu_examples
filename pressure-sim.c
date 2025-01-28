#include "pressure-sim-utils.h"

#include <stdlib.h> 
#include <stdio.h> 

#define COLOR_TO_UINT8(color) {(Uint8)((color).r * 255), (Uint8)((color).g * 255), (Uint8)((color).b * 255), (Uint8)((color).a * 255)}
#define RGBA_TO_FLOAT(r, g, b, a) ((float)(r) / 255.0f), ((float)(g) / 255.0f), ((float)(b) / 255.0f), ((float)(a) / 255.0f)
#define BOX_UNPACK(box) box.l, box.r, box.b, box.t

#define N 10 
#define R 10.0f 
#define WINDOW_WIDTH  1200
#define WINDOW_HEIGHT 1000 


typedef struct GPUParticle {
    float x, y, z; // gpu coords 
    float padding; // vulkan needs 16 byte alignment. maybe theres a fix to this, seems wasteful 
} GPUParticle; 


typedef struct Box {
    float l, r, b, t; 
} Box; 


bool box_overlap(Box b1, Box b2) {
    return (
        b1.r >= b2.l &&  
        b1.l <= b2.r &&  
        b1.t >= b2.b && 
        b1.b <= b2.t);
}


typedef struct Vec2ui {
    uint x, y; 
} Vec2ui; 


typedef struct Vec2f {
    float x, y; 
} Vec2f; 


typedef struct ChunkNeighbors ChunkNeighbors; 
typedef struct Particle Particle; 


typedef struct Chunk {
    Box box;
    struct ChunkNeighbors* neighbors; 
    Particle** particles; 
    uint n_filled; 
    uint n_free; 
} Chunk; 


struct ChunkNeighbors {
    Chunk* left; 
    Chunk* right; 
    Chunk* bottom; 
    Chunk* top; 
}; 


struct Particle {
    GPUParticle gpu;
    float x, y, z; // like world coords  
    Box box; 
    Chunk* chunks[4]; 
    Uint8 n_chunks;
    float vx, vy; 
    float m; 
}; 


typedef struct Container {
    uint width, height; 
    float zoom, inverse_aspect_ratio, scalar; 
} Container; 


int chunk_add_particle(Chunk* chunk, Particle* p) {
    if (chunk->n_free == 0) {
        chunk->particles = realloc(chunk->particles, 2 * chunk->n_filled * sizeof(Particle*));
        if (chunk->particles == NULL) {
            fprintf(stderr, "ERROR: Chunk reallocation failed.\n");
            return -1; 
        }
        chunk->n_free = chunk->n_filled;
    } 
    chunk->particles[chunk->n_filled] = p; 
    chunk->n_filled++; 
    chunk->n_free--;
    return 0; 
}


int chunk_remove_particle(Chunk* chunk, uint i) {
    chunk->n_filled--;
    chunk->particles[i] = chunk->particles[chunk->n_filled]; 
    chunk->particles[chunk->n_filled] = NULL;
    chunk->n_free++;
    return 0; 
}



void handle_event(SDL_Event event, bool* quit, uint* sim_state) {
    switch (event.type) {
        case SDL_EVENT_QUIT:  
            *quit = true; 
            break; 
        case SDL_EVENT_KEY_DOWN:
            switch (event.key.key) {
                case SDLK_Q:    
                    *quit = true; 
                    break; 
                case SDLK_W:    
                    break; 
                case SDLK_S:    
                    break; 
                case SDLK_D:    
                    break; 
                case SDLK_SPACE:    
                    if (*sim_state == 0)
                        *sim_state = 1; 
                    else if (*sim_state == 1)
                        *sim_state = 0; 
                    break; 
            }
            break; 
    } 
}


float rand_float(float min, float max) {
    return min + (max - min) * (rand() / (float)RAND_MAX);
}


void collide(Particle* p1, Particle* p2) {

}


int physics_tick(float dt, Particle* particles, uint n_particles, float p_radius, Container* container, Chunk** chunkmap, Vec2ui chunks, Chunk** border_chunks, uint n_border_chunks) {
    //  we iterate in a grid pattern:
    //  
    //  x x x x 
    //  x x x x 
    //  x x x x 
    //  
    //  o x o x 
    //  x o x o 
    //  o x o x 
    //

    for (uint i = 0; i < chunks.x; i++) {
        for (uint j = i%2; j < chunks.y; j+=2) {
            Chunk chunk = chunkmap[i][j];
            for (uint k = 0; k < chunk.n_filled; k++) {
                Particle* p = chunk.particles[k]; 
                for (uint l = 0; l < k; l++) {
                    collide(p, chunk.particles[l]);
                }
                for (uint l = k+1; l < chunk.n_filled; l++) {
                    collide(p, chunk.particles[l]);
                }
                if (chunk.neighbors->left != NULL) {
                    Chunk* neighbor = chunk.neighbors->left; 
                    for (uint l = 0; l < neighbor->n_filled; l++) {
                        collide(p, neighbor->particles[l]);
                    }
                }
                if (chunk.neighbors->right != NULL) {
                    Chunk* neighbor = chunk.neighbors->right; 
                    for (uint l = 0; l < neighbor->n_filled; l++) {
                        collide(p, neighbor->particles[l]);
                    }
                }
                if (chunk.neighbors->bottom != NULL) {
                    Chunk* neighbor = chunk.neighbors->bottom; 
                    for (uint l = 0; l < neighbor->n_filled; l++) {
                        collide(p, neighbor->particles[l]);
                    }
                } 
                if (chunk.neighbors->top != NULL) {
                    Chunk* neighbor = chunk.neighbors->top; 
                    for (uint l = 0; l < neighbor->n_filled; l++) {
                        collide(p, neighbor->particles[l]);
                    }
                } 
            }
        }
    }

    for (uint i = 0; i < n_border_chunks; i++) {
        Chunk* chunk = border_chunks[i]; 
        printf("(%d) %d\n", i, chunk->n_filled); 
        for (uint j = 0; j < chunk->n_filled; j++) {
            printf("%d %d %d\n", j, chunk->n_filled, chunk->n_free);
            Particle* p = chunk->particles[j]; 
            if (p->box.l <= 0) 
                p->vx *= -1; 
            if (p->box.r >= container->width) 
                p->vx *= -1; 
            if (p->box.b <= 0) 
                p->vy *= -1; 
            if (p->box.t >= container->height) 
                p->vy *= -1; 
        }
        printf("B\n");
    }

    for (uint i = 0; i < n_particles; i++) {
        Particle* p = &particles[i]; 
        p->x += p->vx*dt;  
        p->y += p->vy*dt;  

        p->box.l = p->x-p_radius;
        p->box.r = p->x+p_radius;
        p->box.b = p->y-p_radius;
        p->box.t = p->y+p_radius;

        p->gpu.x = p->x*container->scalar-1.0f; 
        p->gpu.y = p->y*container->zoom-1.0f;
    }
    for (uint i = 0; i < chunks.x; i++) {
        for (uint j = 0; j < chunks.y; j++) {
            Chunk chunk = chunkmap[i][j];
            for (uint k = 0; k < chunk.n_filled; k++) {
                Particle* p_ptr = chunk.particles[k]; 
                Particle p = *p_ptr; 
                bool left_x = false; 
                bool left_y = false; 
                if (!left_x && chunk.neighbors->right != NULL) {
                    if (p.box.l > chunk.box.r) { 
                        Chunk* neighbor = chunk.neighbors->right;
                        chunk_remove_particle(&chunk, k);
                        if (chunk_add_particle(neighbor, p_ptr) < 0) return 1;
                        for (int l = 0; l < p.n_chunks; l++) {
                            if (p.chunks[l] == &chunk) {
                                p.chunks[l] = neighbor; 
                                break; 
                            }
                        }
                        left_x = true; 
                    }
                }
                if (!left_x && chunk.neighbors->left != NULL) {
                    if (p.box.r < chunk.box.l) { 
                        Chunk* neighbor = chunk.neighbors->right;
                        chunk_remove_particle(&chunk, k);
                        if (chunk_add_particle(neighbor, p_ptr) < 0) return 1;
                        for (int l = 0; l < p.n_chunks; l++) {
                            if (p.chunks[l] == &chunk) {
                                p.chunks[l] = neighbor; 
                                break; 
                            }
                        }
                        left_x = true; 
                    }
                }
                if (!left_y && chunk.neighbors->top != NULL) {
                    if (p.box.b < chunk.box.t) { 
                        Chunk* neighbor = chunk.neighbors->right;
                        chunk_remove_particle(&chunk, k);
                        if (chunk_add_particle(neighbor, p_ptr) < 0) return 1;
                        for (int l = 0; l < p.n_chunks; l++) {
                            if (p.chunks[l] == &chunk) {
                                p.chunks[l] = neighbor; 
                                break; 
                            }
                        }
                        left_y = true; 
                    }
                }
                if (!left_y && chunk.neighbors->bottom != NULL) {
                    if (p.box.t < chunk.box.b) { 
                        Chunk* neighbor = chunk.neighbors->right;
                        chunk_remove_particle(&chunk, k);
                        if (chunk_add_particle(neighbor, p_ptr) < 0) return 1;
                        for (int l = 0; l < p.n_chunks; l++) {
                            if (p.chunks[l] == &chunk) {
                                p.chunks[l] = neighbor; 
                                break; 
                            }
                        }
                        left_y = true; 
                    }
                }
            }
        }
    }
    return 0;
}


int setup_particles(Particle* particles, uint n_particles, float particle_radius, Container* container, Chunk** chunkmap, Vec2ui chunks) {
    uint particles_per_row = 1.0f/(particle_radius*container->scalar);
    uint particles_per_col = 1.0f/(particle_radius*container->zoom);

    uint n_particles_possible = particles_per_row*particles_per_col;
    if (n_particles_possible < n_particles) {
        fprintf(stderr, "Too many particles %d for container %d\n", n_particles, n_particles_possible); 
        return -1; 
    }


    float v_start = 1000.0f; 
    for (uint i = 0; i < n_particles; i++) { 
        Particle* p = &particles[i]; 

        uint col = i%particles_per_row;
        uint row = (uint) (i/particles_per_row);
        p->x = particle_radius*(1.0f + 2.0f*col); 
        p->y = particle_radius*(1.0f + 2.0f*row); 

        p->box = (Box) { 
            .l = p->x-particle_radius, 
            .r = p->x+particle_radius,
            .b = p->y-particle_radius,
            .t = p->y+particle_radius 
        };
        p->gpu.x = -1.0f + p->x * container->scalar; 
        p->gpu.y = -1.0f + p->y * container->zoom;
        p->gpu.z = 0.0f; 

        p->vx = rand_float(-v_start, v_start); 
        p->vy = rand_float(-v_start, v_start); 
    }

    // This is O(N^3) 
    int c = 0; 
    for (uint i = 0; i < chunks.x; i++) {
        for (uint j = 0; j < chunks.y; j++) {
            Chunk chunk = chunkmap[i][j]; 
            for (uint k = 0; k < n_particles; k++) {
                Particle* p = &particles[k]; 
                /* printf("%f %f %f %f %f %f %f %f %d %d %d\n", BOX_UNPACK(p->box), BOX_UNPACK(chunk.box), i, j, k); */ 
                if (box_overlap(p->box, chunk.box)) {
                    c++; 
                    chunk_add_particle(&chunk, p);
                    printf("%d\n", c);
                    p->chunks[p->n_chunks] = &chunk; 
                    p->n_chunks++; 
                }
            }
        } 
    } 

    for (uint i = 0; i < chunks.x; i++) {
        for (uint j = 0; j < chunks.y; j++) {
            Chunk chunk = chunkmap[i][j]; 
            /* printf("%d,%d  %d\n", i,j,chunk.n_filled); */
        } 
    } 
    return 0; 
}


int setup_chunkmap(Chunk** chunkmap, Vec2ui chunks, Vec2f chunk_size, Chunk** border_chunks, Particle* particles) {
    uint i_border_chunks = 0; 
    
    for (uint i = 0; i < chunks.x; i++) {
        chunkmap[i] = malloc(chunks.y * sizeof(Chunk)); 
        if (chunkmap[i] == NULL) {
            fprintf(stderr, "ERROR: Failed to allocate chunkmap box.");
            return -1; 
        }
        for (uint j = 0; j < chunks.y; j++) {
            Box box = (Box) {
                .l = i*chunk_size.x, 
                .r = (i+1)*chunk_size.x,
                .b = j*chunk_size.y,
                .t = (j+1)*chunk_size.y
            };
            Particle** chunk_particles = malloc(2*sizeof(Particle*));
            if (chunk_particles== NULL) {
                fprintf(stderr, "ERROR: malloc failed.\n");
                return -1; 
            }
            Chunk* chunk = malloc(sizeof(Chunk));
            *chunk = (Chunk) { 
                .box = box, 
                .particles = chunk_particles, 
                .n_filled = 0, 
                .n_free = 2 
            };
            chunkmap[i][j] = *chunk;            

            if (i == 0 || i == chunks.x - 1) {
                border_chunks[i_border_chunks] = chunk; 
                i_border_chunks++; 
            } else if (j == 0 || j == chunks.y - 1) {
                border_chunks[i_border_chunks] = chunk; 
                i_border_chunks++; 
            }

            /* printf("(%d,%d) %f %f %f %f\n", i, j, box.l, box.r, box.b, box.t); */
        }
    }
    for (uint i = 0; i < chunks.x; i++) {
        for (uint j = 0; j < chunks.y; j++) {
            ChunkNeighbors neighbors = {
                .left = NULL,
                .right = NULL,
                .bottom = NULL,
                .top = NULL,
            }; 
            if (i > 0) 
                neighbors.left = &chunkmap[i-1][j];
            if (j > 0) 
                neighbors.bottom = &chunkmap[i][j-1];
            if (i + 1 < chunks.x) 
                neighbors.right = &chunkmap[i+1][j];
            if (j + 1 < chunks.y) 
                neighbors.top = &chunkmap[i][j+1];

            chunkmap[i][j].neighbors = &neighbors; 
        }
    }
    return 0; 
}


void destroy_sdl(
    SDL_GPUDevice* device, 
    SDL_GPUGraphicsPipeline* pipeline, 
    SDL_GPUBuffer* vertex_buffer, 
    SDL_GPUBuffer* index_buffer, 
    SDL_GPUTransferBuffer* live_data_transfer_buffer, 
    SDL_GPUBuffer* live_data_buffer, 
    SDL_Window* window) {

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


void free_particles(Particle* particles, uint n_particles) {
    for (int i = 0; i < n_particles; i++) {
        free(particles[i].chunks); 
    }
    free(particles);
}


void free_chunkmap(Chunk** chunkmap, Vec2ui chunks, Chunk** border_chunks) {
    printf("C\n");
    free(border_chunks);
    for (uint i = 0; i < chunks.x; i++) {
        for (uint j = 0; j < chunks.y; j++) {
            Chunk chunk = chunkmap[i][j]; 
            free(chunk.particles); 
        }
        free(chunkmap[i]); 
    } 
    free(chunkmap); 
    printf("C\n");
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

    SDL_FColor COLOR_TRANSPARENT = (SDL_FColor) { 0.0f, 0.0f, 0.0f, 0.0f }; 
    SDL_FColor COLOR_WHITE       = (SDL_FColor) { 1.0f, 1.0f, 1.0f, 1.0f }; 
    SDL_FColor COLOR_BLACK       = (SDL_FColor) { 0.0f, 0.0f, 0.0f, 1.0f }; 
    SDL_FColor COLOR_RED         = (SDL_FColor) { 1.0f, 0.0f, 0.0f, 1.0f }; 
    SDL_FColor COLOR_GREEN       = (SDL_FColor) { 0.0f, 1.0f, 0.0f, 1.0f }; 
    SDL_FColor COLOR_BLUE        = (SDL_FColor) { 0.0f, 0.0f, 1.0f, 1.0f }; 
    SDL_FColor COLOR_CYAN        = (SDL_FColor) { 0.0f, 1.0f, 1.0f, 1.0f }; 
    SDL_FColor COLOR_YELLOW      = (SDL_FColor) { 1.0f, 1.0f, 0.0f, 1.0f }; 
    SDL_FColor COLOR_PINK        = (SDL_FColor) { 1.0f, 0.0f, 1.0f, 1.0f }; 
    SDL_FColor COLOR_GRAY        = (SDL_FColor) { RGBA_TO_FLOAT(36, 36, 36, 255) }; 

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
                    .offset = sizeof(float) * 3 + sizeof(float) * 2 + sizeof(Uint8) * 4 
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

    uint n_vertices = 4; 
    uint n_indices  = 6; 
    SDL_GPUBuffer* vertex_buffer = SDL_CreateGPUBuffer(
        device, 
        &(SDL_GPUBufferCreateInfo) {
            .usage = SDL_GPU_BUFFERUSAGE_VERTEX, 
            .size = sizeof(PositionTextureVertex) * n_vertices
        }
    ); 

    SDL_GPUBuffer* index_buffer = SDL_CreateGPUBuffer(
        device, 
        &(SDL_GPUBufferCreateInfo) {
            .usage = SDL_GPU_BUFFERUSAGE_INDEX, 
            .size = sizeof(Uint16) * n_indices 
        }
    ); 

    SDL_GPUTransferBuffer* transfer_buffer = SDL_CreateGPUTransferBuffer(
        device,
        &(SDL_GPUTransferBufferCreateInfo) {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = (sizeof(PositionTextureVertex) * n_vertices) + (sizeof(Uint16) * n_indices)
        }
    );

    PositionTextureVertex* transfer_data = SDL_MapGPUTransferBuffer(device, transfer_buffer, false);
    
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
    float radius = R; 
    for (int i = 0; i < n_vertices; i++) {
        transfer_data[i].x *= container.inverse_aspect_ratio;   
        transfer_data[i].x *= radius*container.zoom;  
        transfer_data[i].y *= radius*container.zoom;  
    }

    Uint16* index_data = (Uint16*) &transfer_data[n_vertices];
    index_data[0] = 2;
    index_data[1] = 1;
    index_data[2] = 0;
    index_data[3] = 2;
    index_data[4] = 0;
    index_data[5] = 3;

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
            .size = sizeof(PositionTextureVertex) * n_vertices 
        },
        false
    );
    SDL_UploadToGPUBuffer(
        copy_pass,
        &(SDL_GPUTransferBufferLocation) {
            .transfer_buffer = transfer_buffer,
            .offset = sizeof(PositionTextureVertex) * n_vertices
        },
        &(SDL_GPUBufferRegion) {
            .buffer = index_buffer,
            .offset = 0,
            .size = sizeof(Uint16) * n_indices
        },
        false
    );
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(upload_cmdbuf);
    SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);

    uint n_instances = N; // Depends on the GPU i guess, I have 8GB VRAM so 1 million should be fine 
    SDL_GPUBuffer* live_data_buffer = SDL_CreateGPUBuffer(
        device,
        &(SDL_GPUBufferCreateInfo) {
            .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
            .size = n_instances * sizeof(GPUParticle)
        }
    );

    SDL_GPUTransferBuffer* live_data_transfer_buffer = SDL_CreateGPUTransferBuffer(
        device,
        &(SDL_GPUTransferBufferCreateInfo) {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = n_instances * sizeof(GPUParticle)
        }
    );


    // 
    // ----  Vulkan setup done  -----
    // 

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    uint viewport_width      = WINDOW_WIDTH; 
    uint viewport_height     = WINDOW_HEIGHT; 
    float viewport_min_depth = 0.1f; 
    float viewport_max_depth = 1.0f; 
    SDL_GPUViewport small_viewport = (SDL_GPUViewport) { 
        (WINDOW_WIDTH-viewport_width)/2.0f, (WINDOW_HEIGHT-viewport_height)/2.0f, 
        viewport_width, viewport_height, 
        viewport_min_depth, viewport_max_depth 
    };


    // Setup simulation 
    Particle* particles = (Particle*) calloc(n_instances, sizeof(Particle));

    if (particles == NULL) {
        fprintf(stderr, "ERROR: Allocating memory for %d particles failed!\n", n_instances);
        destroy_sdl(device, pipeline, vertex_buffer, index_buffer, live_data_transfer_buffer, live_data_buffer, window); 
        return 1;
    }

    Vec2ui chunks = { .x = 10, .y = 8 }; 
    Vec2f chunk_size = { .x = (float) container.width / chunks.x, .y = (float) container.height / chunks.y }; 

    Chunk** chunkmap = malloc(chunks.x * sizeof(Chunk *)); 
    uint n_border_chunks = 2*(chunks.x+chunks.y)-4; 
    Chunk** border_chunks = malloc(n_border_chunks * sizeof(Chunk*));

    if (chunkmap == NULL || border_chunks == NULL) {
        fprintf(stderr, "ERROR: Failed to allocate chunkmap.");
        free_particles(particles, n_instances);
        destroy_sdl(device, pipeline, vertex_buffer, index_buffer, live_data_transfer_buffer, live_data_buffer, window); 
        return 1; 
    }

    if (setup_chunkmap(chunkmap, chunks, chunk_size, border_chunks, particles) < 0) {
        free_particles(particles, n_instances);
        free_chunkmap(chunkmap, chunks, border_chunks); 
        destroy_sdl(device, pipeline, vertex_buffer, index_buffer, live_data_transfer_buffer, live_data_buffer, window); 
        return 1; 
    }

    printf("chunkmap initialized!\n");
    int sim_setup = setup_particles(particles, n_instances, radius, &container, chunkmap, chunks);
    if (sim_setup < 0) {
        fprintf(stderr, "ERROR: sim setup failed.\n");
        free_particles(particles, n_instances);
        free_chunkmap(chunkmap, chunks, border_chunks); 
        destroy_sdl(device, pipeline, vertex_buffer, index_buffer, live_data_transfer_buffer, live_data_buffer, window); 
        return 1; 
    }

    uint sim_state = 0; 
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
            physics_tick(dt, particles, n_instances, radius, &container, chunkmap, chunks, border_chunks, n_border_chunks); 
        }

        GPUParticle* live_data = SDL_MapGPUTransferBuffer(
            device,
            live_data_transfer_buffer,
            true
        );

        // Write to live_data here! 
        for (uint i = 0; i < n_instances; i+=1) {
            live_data[i].x = particles[i].gpu.x;
            live_data[i].y = particles[i].gpu.y;
            live_data[i].z = particles[i].gpu.z; 
        }

        /* for (Uint32 i = 0; i < n_instances; i+=1) { */
        /*  printf("%d x=%f y=%f z=%f\n", i, live_data[i].x, live_data[i].y, live_data[i].z); */
        /* } */
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
                .size = sizeof(GPUParticle) * n_instances
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

        SDL_DrawGPUIndexedPrimitives(render_pass, n_indices, n_instances, 0, 0, 0);
        SDL_EndGPURenderPass(render_pass);

        SDL_SubmitGPUCommandBuffer(cmdbuf);
    }
    
    free_particles(particles, n_instances);
    free_chunkmap(chunkmap, chunks, border_chunks);
    destroy_sdl(device, pipeline, vertex_buffer, index_buffer, live_data_transfer_buffer, live_data_buffer, window); 
    return 0; 
}

