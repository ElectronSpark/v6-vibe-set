#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct SDL_Window SDL_Window;

int main(void)
{
    void *sdl;
    int (*init)(uint32_t);
    SDL_Window *(*create)(const char *, int, int, int, int, uint32_t);
    void (*get_size)(SDL_Window *, int *, int *);
    void (*destroy)(SDL_Window *);
    void (*quit)(void);
    SDL_Window *window;
    int width = 0, height = 0;

    sdl = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (sdl == NULL) {
        fprintf(stderr, "selftest: dlopen: %s\n", dlerror());
        return 2;
    }
    *(void **)(&init) = dlsym(sdl, "SDL_Init");
    *(void **)(&destroy) = dlsym(sdl, "SDL_DestroyWindow");
    *(void **)(&quit) = dlsym(sdl, "SDL_Quit");
    *(void **)(&create) = dlsym(RTLD_DEFAULT, "SDL_CreateWindow");
    *(void **)(&get_size) = dlsym(RTLD_DEFAULT, "SDL_GetWindowSize");
    if (init == NULL || create == NULL || get_size == NULL ||
        destroy == NULL || quit == NULL)
        return 3;
    if (init(0x00000020U) != 0)
        return 4;
    window = create("xv6-sdl-geometry-selftest", 0x1fff0000, 0x1fff0000,
                    320, 240, 0x00000008U);
    if (window == NULL)
        return 5;
    get_size(window, &width, &height);
    printf("selftest: logical=%dx%d\n", width, height);
    destroy(window);
    quit();
    dlclose(sdl);
    return width == 320 && height == 240 ? 0 : 6;
}
