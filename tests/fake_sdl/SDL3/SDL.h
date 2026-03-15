#pragma once

#include <cstdarg>
#include <cstdio>

struct SDL_Window {};
struct SDL_Renderer {};
struct SDL_Texture {};

struct SDL_FRect {
    float x {};
    float y {};
    float w {};
    float h {};
};

struct SDL_Rect {
    int x {};
    int y {};
    int w {};
    int h {};
};

struct SDL_Color {
    unsigned char r {};
    unsigned char g {};
    unsigned char b {};
    unsigned char a {};
};

struct SDL_KeyboardEvent {
    int key {};
};

struct SDL_MouseButtonEvent {
    unsigned char button {};
    float x {};
    float y {};
};

struct SDL_MouseMotionEvent {
    float x {};
    float y {};
};

struct SDL_MouseWheelEvent {
    float y {};
};

struct SDL_Event {
    int type {};
    SDL_KeyboardEvent key {};
    SDL_MouseButtonEvent button {};
    SDL_MouseMotionEvent motion {};
    SDL_MouseWheelEvent wheel {};
};

inline constexpr int SDL_INIT_VIDEO = 0x00000020;
inline constexpr int SDL_WINDOW_RESIZABLE = 0x00000001;
inline constexpr int SDL_EVENT_QUIT = 0x100;
inline constexpr int SDL_EVENT_MOUSE_WHEEL = 0x200;
inline constexpr int SDL_EVENT_KEY_DOWN = 0x201;
inline constexpr int SDL_EVENT_MOUSE_BUTTON_DOWN = 0x202;
inline constexpr int SDL_EVENT_MOUSE_MOTION = 0x203;
inline constexpr int SDL_EVENT_MOUSE_BUTTON_UP = 0x204;
inline constexpr unsigned char SDL_BUTTON_LEFT = 1;
inline constexpr int SDLK_ESCAPE = 27;
inline constexpr int SDLK_F = 'f';
inline constexpr int SDLK_SPACE = 32;
inline constexpr int SDLK_D = 'd';
inline constexpr int SDLK_N = 'n';

inline auto SDL_Init(unsigned int) -> bool
{
    return true;
}

inline void SDL_Quit() {}

inline auto SDL_GetError() -> const char*
{
    return "fake SDL";
}

inline auto SDL_CreateWindow(const char*, int, int, unsigned int) -> SDL_Window*
{
    static SDL_Window window;
    return &window;
}

inline void SDL_DestroyWindow(SDL_Window*) {}

inline auto SDL_CreateRenderer(SDL_Window*, const char*) -> SDL_Renderer*
{
    static SDL_Renderer renderer;
    return &renderer;
}

inline void SDL_DestroyRenderer(SDL_Renderer*) {}
inline void SDL_DestroyTexture(SDL_Texture*) {}

inline auto SDL_PollEvent(SDL_Event* event) -> bool
{
    static bool sent_quit = false;
    if (sent_quit) {
        return false;
    }
    sent_quit = true;
    if (event != nullptr) {
        *event = SDL_Event {};
        event->type = SDL_EVENT_QUIT;
    }
    return true;
}

inline void SDL_Delay(unsigned int) {}

inline void SDL_GetWindowSize(SDL_Window*, int* width, int* height)
{
    if (width != nullptr) {
        *width = 1440;
    }
    if (height != nullptr) {
        *height = 900;
    }
}

inline void SDL_SetRenderDrawColor(SDL_Renderer*, unsigned char, unsigned char, unsigned char, unsigned char) {}
inline void SDL_RenderFillRect(SDL_Renderer*, const SDL_FRect*) {}
inline void SDL_RenderRect(SDL_Renderer*, const SDL_FRect*) {}
inline void SDL_RenderClear(SDL_Renderer*) {}
inline void SDL_RenderPresent(SDL_Renderer*) {}
inline void SDL_SetRenderClipRect(SDL_Renderer*, const SDL_Rect*) {}
inline void SDL_RenderTexture(SDL_Renderer*, SDL_Texture*, const SDL_FRect*, const SDL_FRect*) {}

inline void SDL_Log(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    std::fputc('\n', stderr);
    va_end(args);
}
