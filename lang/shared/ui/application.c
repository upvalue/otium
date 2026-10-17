#include <SDL3/SDL.h>

bool otui_app_configure(const char* name, const char* version, const char* identifier,
                        bool background) {
  // Set before SDL_Init: Cocoa creates its application menu during video init.
  SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, background ? "1" : "0");
  return SDL_SetAppMetadata(name, version, identifier);
}
bool otui_window_present(SDL_Window* window, const char* icon, bool foreground) {
  if (icon[0]) {
    SDL_Surface* image = SDL_LoadSurface(icon);
    if (!image) return false;
    bool ok = SDL_SetWindowIcon(window, image);
    SDL_DestroySurface(image);
    if (!ok) return false;
  }
  return !foreground || SDL_RaiseWindow(window);
}
