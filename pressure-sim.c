#include <SDL3/SDL.h> 
#include <stdlib.h> 
#include <stdio.h> 
#include <strings.h>

#define COLOR_TO_UINT8(color) {(Uint8)((color).r * 255), (Uint8)((color).g * 255), (Uint8)((color).b * 255), (Uint8)((color).a * 255)}
#define RGBA_TO_FLOAT(r, g, b, a) ((float)(r) / 255.0f), ((float)(g) / 255.0f), ((float)(b) / 255.0f), ((float)(a) / 255.0f)

typedef struct PositionTextureVertex {
    float x, y, z;
    float u, v;
    Uint8 color1[4];
    Uint8 color2[4];
} PositionTextureVertex;


typedef struct GPUParticle {
    float x, y, z; // gpu coords 
    float padding; // vulkan needs 16 byte alignment. maybe theres a fix to this, seems wasteful 
} GPUParticle; 


typedef struct Particle {
    GPUParticle gpu;
    float x, y, z; // like world coords  
    float vx, vy; 
    float m; 
} Particle; 


typedef struct Container {
    uint width, height; 
    float zoom, inverse_aspect_ratio, scalar; 
} Container; 


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

    SDL_GPUShaderCreateInfo shader_info = {
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


void print_device_driver_info() {
    int render_drivers = SDL_GetNumRenderDrivers(); 
    printf("Number of render drivers: %i\n", render_drivers); 

    for (int i = 0; i < render_drivers; i++) {
        printf("Render driver #%i: %s\n", i, SDL_GetRenderDriver(i)); 
    }

    int video_drivers = SDL_GetNumVideoDrivers(); 
    printf("Number of video drivers: %i\n", video_drivers); 

    for (int i = 0; i < video_drivers; i++) {
        printf("Video driver #%i: %s\n", i, SDL_GetVideoDriver(i)); 
    }

    printf("Current video driver: %s\n", SDL_GetCurrentVideoDriver());
}


float rand_float(float min, float max) {
    return min + (max - min) * (rand() / (float)RAND_MAX);
}


void physics_tick(float dt, Particle* particles, uint n_particles) {
    for (int i = 0; i < n_particles; i++) {
        Particle* particle = &particles[i]; 
        particle->x += particle->vx*dt;  
        particle->y += particle->vy*dt;  
        particle->gpu.x = particle->x;  
        particle->gpu.y = particle->y;  
        particle->gpu.z = 0.0f;  
    }
}


int setup_particles(Particle* particles, uint n_particles, float particle_radius, Container* container) {
    /* uint n_particles_possible = container->width*container->height/(4*particle_radius*particle_radius); */ 
    /* if (n_particles_possible < n_particles) { */
    /*     fprintf(stderr, "Too many particles %d for container %d\n", n_particles, n_particles_possible); */ 
    /*     return -1; */ 
    /* } */
    /* int particles_per_row = (container->width - particle_radius) / (8 * particle_radius); */

    int particles_per_row = (1.0f/(particle_radius*container->scalar)-0.5f);
    printf("%f %f\n", container->scalar, container->zoom);

    for (uint i = 0; i < n_particles; i++) { 
        Particle* particle = &particles[i]; 
        
        /* int row = i / particles_per_row; */
        /* int col = i % particles_per_row; */

        particle->gpu.x = -1.0f + particle_radius*container->scalar + (i%particles_per_row)*2*particle_radius*container->scalar;
        particle->gpu.x = 1.0f; 
        particle->gpu.y = -1.0f + particle_radius*container->zoom; 


        particle->vx = rand_float(-1.0f, 1.0f); 
        particle->vy = rand_float(-1.0f, 1.0f); 
        particle->vx = 0.0f; 
        particle->vy = 0.0f; 
    }
    return 0; 
}


void destory_everything(
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


int main(int argc, char* argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "ERROR: SDL_Init failed: %s\n", SDL_GetError());
        return 1; 
    } 

    uint WINDOW_WIDTH        = 1400; 
    uint WINDOW_HEIGHT       = 1000; 
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

    printf("OK: Created device with driver '%s'\n", SDL_GetGPUDeviceDriver(device));
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
    float radius = 30.0f; 
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

    uint n_instances = 1000; // Depends on the GPU i guess, I have 8GB VRAM so 1 million should be fine 
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


    // The simulation 
    bool quit = false; 
    int index_offset  = 0; 
    int vertex_offset = 0; 

    float dt = 0.001f; 

    uint sim_state = 0; 

    Particle* particles; 
    particles = (Particle*) calloc(n_instances, sizeof(Particle));

    if (particles == NULL) {
        fprintf(stderr, "ERROR: calloc particles failed!\n");
        destory_everything(device, pipeline, vertex_buffer, index_buffer, live_data_transfer_buffer, live_data_buffer, window); 
        return 1;
    }

    int sim_setup = setup_particles(particles, n_instances, radius, &container);
    if (sim_setup < 0) {
        fprintf(stderr, "ERROR: sim setup failed.\n");
        destory_everything(device, pipeline, vertex_buffer, index_buffer, live_data_transfer_buffer, live_data_buffer, window); 
        return 1; 
    }

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
            physics_tick(dt, particles, n_instances); 
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
    
    destory_everything(device, pipeline, vertex_buffer, index_buffer, live_data_transfer_buffer, live_data_buffer, window); 
    return 0; 
}

