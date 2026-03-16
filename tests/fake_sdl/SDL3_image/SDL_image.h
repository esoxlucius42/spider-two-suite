#pragma once

#include "../SDL3/SDL.h"

inline auto IMG_LoadTexture(SDL_Renderer*, const char*) -> SDL_Texture*
{
    static SDL_Texture texture;
    return &texture;
}

inline auto IMG_Load(const char*) -> SDL_Surface*
{
    static SDL_Surface surface;
    return &surface;
}
