#include "dpi.h"

#ifdef __APPLE__
#include "macos.h"

float get_display_ppi(SDL_Window *window) {
    return macos_get_display_ppi(window);
}

#elif defined(HAVE_X11)
#include <X11/Xlib.h>
#include <SDL3/SDL.h>

float get_display_ppi(SDL_Window *window) {
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    Display *display = (Display *)SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
    if (!display) return 0.0f;

    int screen = (int)SDL_GetNumberProperty(
        props, SDL_PROP_WINDOW_X11_SCREEN_NUMBER, DefaultScreen(display));

    int px_wide = DisplayWidth(display, screen);
    int mm_wide = DisplayWidthMM(display, screen);
    if (mm_wide <= 0) return 0.0f;

    return (float)(px_wide * 25.4 / mm_wide);
}

#else

float get_display_ppi(SDL_Window *window) {
    (void)window;
    return 0.0f;
}

#endif
