// A readable version of https://github.com/TheSpydog/SDL_gpu_examples/blob/main/Examples/BasicTriangle.c

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_video.h>
#include <stdio.h> 
#include <stdlib.h> 
#include <SDL3/SDL.h> 

#define WINDOW_WIDTH 1200 
#define WINDOW_HEIGHT 800 
#define WINDOW_TITLE "Basic Triangle"


SDL_GPUShader* load_shader(
    SDL_GPUDevice* device, 
    const char* filename, 
    SDL_GPUShaderStage stage, 
    Uint32 sampler_count, 
    Uint32 uniform_buffer_count, 
    Uint32 storage_buffer_count, 
    Uint32 storage_texture_count) {

    if(!SDL_GetPathInfo(filename, NULL)) {
        fprintf(stdout, "File (%s) does not exist.\n", filename);
        return NULL;    
    }
        
    const char* entrypoint; 
    SDL_GPUShaderFormat backend_formats = SDL_GetGPUShaderFormats(device); 
    SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID; 
    if (backend_formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        format = SDL_GPU_SHADERFORMAT_SPIRV;
        entrypoint = "main"; 
    }

    size_t code_size; 
    void* code = SDL_LoadFile(filename, &code_size); 
    if (code == NULL) {
        fprintf(stderr, "ERROR: SDL_LoadFile(%s) failed: %s\n", filename, SDL_GetError());
        return NULL;  
    }

    SDL_GPUShaderCreateInfo shader_info = {
        .code = code,
        .code_size = code_size,
        .entrypoint = entrypoint,
        .format = format,
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


void print_info(void) {
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


void handle_event(SDL_Event event, bool* quit) {
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
            }
        } break; 
    } 
}


int main(int argc, char* argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "ERROR: SDL_Init failed: %s\n", SDL_GetError());
        return 1; 
    } 

    SDL_Window* window; 
    window = SDL_CreateWindow(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_VULKAN);

    if (window == NULL) {
        fprintf(stderr, "ERROR: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;  
    }

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL); 

    if (device == NULL) {
        fprintf(stderr, "ERROR: SDL_CreateGPUDevice failed: %s\n", SDL_GetError());
        return 1; 
    }

    const char* device_driver = SDL_GetGPUDeviceDriver(device); 
    printf("OK: Created device with driver %s\n", device_driver);

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        fprintf(stderr, "ERROR: SDL_ClaimWindowForGPUDevice failed: %s\n", SDL_GetError());
        return 1; 
    }


    // Load shaders + create fill/line pipeline 
    SDL_GPUShader* shader_vert = load_shader(device, "shaders/compiled/RawTriangle.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 0, 0); 
    if (shader_vert == NULL) {
        fprintf(stderr, "ERROR: LoadShader failed \n");
        return 1;   
    }

    SDL_GPUShader* shader_frag = load_shader(device, "shaders/compiled/SolidColor.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 0); 
    if (shader_vert == NULL) {
        fprintf(stderr, "ERROR: LoadShader failed \n");
        return 1;   
    }

    SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {
        .target_info = {
            .num_color_targets = 1,
            .color_target_descriptions = (SDL_GPUColorTargetDescription[]){{
                .format = SDL_GetGPUSwapchainTextureFormat(device, window)
            }},
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .vertex_shader = shader_vert,
        .fragment_shader = shader_frag,
    };  

    SDL_GPUGraphicsPipeline* pipeline_fill;
    SDL_GPUGraphicsPipeline* pipeline_line;

    pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline_fill = SDL_CreateGPUGraphicsPipeline(device, &pipeline_info);
    if (pipeline_fill == NULL) {
        fprintf(stderr, "ERROR: SDL_CreateGPUGraphicsPipeline failed: %s\n", SDL_GetError());
        return -1;
    }

    pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_LINE;
    pipeline_line = SDL_CreateGPUGraphicsPipeline(device, &pipeline_info);
    if (pipeline_line == NULL) {
        fprintf(stderr, "ERROR: SDL_CreateGPUGraphicsPipeline failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_ReleaseGPUShader(device, shader_vert); 
    SDL_ReleaseGPUShader(device, shader_frag); 

    SDL_FColor COLOR_WHITE  = (SDL_FColor) { 1.0f, 1.0f, 1.0f, 1.0f }; 
    SDL_FColor COLOR_BLACK  = (SDL_FColor) { 0.0f, 0.0f, 0.0f, 1.0f }; 
    SDL_FColor COLOR_RED    = (SDL_FColor) { 1.0f, 0.0f, 0.0f, 1.0f }; 
    SDL_FColor COLOR_GREEN  = (SDL_FColor) { 0.0f, 1.0f, 0.0f, 1.0f }; 
    SDL_FColor COLOR_BLUE   = (SDL_FColor) { 0.0f, 0.0f, 1.0f, 1.0f }; 
    SDL_FColor COLOR_CYAN   = (SDL_FColor) { 0.0f, 1.0f, 1.0f, 1.0f }; 
    SDL_FColor COLOR_YELLOW = (SDL_FColor) { 1.0f, 1.0f, 0.0f, 1.0f }; 
    SDL_FColor COLOR_PINK   = (SDL_FColor) { 1.0f, 0.0f, 1.0f, 1.0f }; 

    bool quit = false; 
    while (!quit) {
        SDL_Event event;
        if (SDL_PollEvent(&event)) handle_event(event, &quit); 
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
            fprintf(stderr, "ERROR: swapchain_texture is NULL\n");
            SDL_SubmitGPUCommandBuffer(cmdbuf);
            break; 
        }

        SDL_GPUColorTargetInfo color_target_info = { 0 };
        color_target_info.texture = swapchain_texture;
        color_target_info.clear_color = COLOR_BLACK; 
        color_target_info.load_op = SDL_GPU_LOADOP_CLEAR;
        color_target_info.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(cmdbuf, &color_target_info, 1, NULL);
        SDL_BindGPUGraphicsPipeline(render_pass, pipeline_fill);
        SDL_DrawGPUPrimitives(render_pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(render_pass);

        SDL_SubmitGPUCommandBuffer(cmdbuf);
    }

    SDL_ReleaseGPUGraphicsPipeline(device, pipeline_fill);
    SDL_ReleaseGPUGraphicsPipeline(device, pipeline_line);

    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyWindow(window);
    SDL_DestroyGPUDevice(device);
    SDL_Quit();

    return 0; 
}
