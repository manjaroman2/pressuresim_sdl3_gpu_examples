#ifndef PS_UTILS_H_
#define PS_UTILS_H_

#include <SDL3/SDL.h>

#define RGBA_TO_FLOAT(r, g, b, a) ((float)(r) / 255.0f), ((float)(g) / 255.0f), ((float)(b) / 255.0f), ((float)(a) / 255.0f)

extern const SDL_FColor COLOR_TRANSPARENT;
extern const SDL_FColor COLOR_WHITE; 
extern const SDL_FColor COLOR_BLACK; 
extern const SDL_FColor COLOR_RED; 
extern const SDL_FColor COLOR_GREEN; 
extern const SDL_FColor COLOR_BLUE; 
extern const SDL_FColor COLOR_CYAN; 
extern const SDL_FColor COLOR_YELLOW; 
extern const SDL_FColor COLOR_PINK; 
extern const SDL_FColor COLOR_GRAY; 


typedef struct PositionTextureVertex {
    float x, y, z;
    float u, v;
    Uint8 color1[4];
    Uint8 color2[4];
} PositionTextureVertex;


SDL_GPUShader* load_shader(
    SDL_GPUDevice* device, 
    const char* filename, 
    SDL_GPUShaderStage stage, 
    Uint32 sampler_count, 
    Uint32 uniform_buffer_count, 
    Uint32 storage_buffer_count, 
    Uint32 storage_texture_count); 

#endif 
