#ifndef PS_UTILS_H_
#define PS_UTILS_H_

#include <SDL3/SDL.h>

#define COLOR_TO_UINT8(_color) {(uint8_t)((_color).r * 255), (uint8_t)((_color).g * 255), (uint8_t)((_color).b * 255), (uint8_t)((_color).a * 255)}
#define RGBA_TO_FLOAT(_r, _g, _b, _a) ((float)(_r) / 255.0f), ((float)(_g) / 255.0f), ((float)(_b) / 255.0f), ((float)(_a) / 255.0f)

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
