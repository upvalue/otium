#include <SDL3/SDL.h>

bool otui_test_key(SDL_Window* window, const char* name, bool down, bool shift) {
  SDL_Keycode key = name[0] && !name[1] ? (unsigned char)name[0] : SDL_GetKeyFromName(name);
  if (key == SDLK_UNKNOWN) return SDL_SetError("Unknown test key: %s", name);
  SDL_Event event = {0};
  event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  event.key.windowID = SDL_GetWindowID(window);
  event.key.key = key;
  event.key.scancode = SDL_GetScancodeFromKey(key, &event.key.mod);
  if (shift) event.key.mod |= SDL_KMOD_SHIFT;
  event.key.down = down;
  return SDL_PushEvent(&event);
}
bool otui_test_text(SDL_Window* window, const char* text) {
  SDL_Event event = {0};
  event.type = SDL_EVENT_TEXT_INPUT;
  event.text.windowID = SDL_GetWindowID(window);
  event.text.text = text;
  return SDL_PushEvent(&event);
}
bool otui_test_wheel(SDL_Window* window, float x, float y, float dy) {
  SDL_Event event = {0};
  event.type = SDL_EVENT_MOUSE_WHEEL;
  event.wheel.windowID = SDL_GetWindowID(window);
  event.wheel.mouse_x = x;
  event.wheel.mouse_y = y;
  event.wheel.y = dy;
  return SDL_PushEvent(&event);
}
bool otui_test_quit(void) {
  SDL_Event event = {0};
  event.type = SDL_EVENT_QUIT;
  return SDL_PushEvent(&event);
}
bool otui_test_resize(SDL_Window* window, int width, int height) {
  if (!SDL_SetWindowSize(window, width, height) || !SDL_SyncWindow(window)) return false;
  SDL_Event event = {0};
  event.type = SDL_EVENT_WINDOW_RESIZED;
  event.window.windowID = SDL_GetWindowID(window);
  event.window.data1 = width;
  event.window.data2 = height;
  return SDL_PushEvent(&event);
}
bool otui_test_pointer(SDL_Window* window, float x, float y, bool down) {
  SDL_Event event = {0};
  event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
  event.button.windowID = SDL_GetWindowID(window);
  event.button.button = SDL_BUTTON_LEFT;
  event.button.x = x;
  event.button.y = y;
  return SDL_PushEvent(&event);
}

// Exercise the no-swapchain path without depending on a window manager's
// minimize animation or whether it allows background windows to minimize.
bool otui_test_visible(SDL_Window* window, bool visible) {
  return (visible ? SDL_ShowWindow(window) : SDL_HideWindow(window)) && SDL_SyncWindow(window);
}
