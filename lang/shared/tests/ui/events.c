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
