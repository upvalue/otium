#include "text.h"
#include <math.h>
#include <string.h>

// Fonts stay alive while any renderer can reference their glyphs. Shaped runs
// have a bounded direct-mapped cache; atlas entries don't reference these runs.
static struct { TTF_Font* font; float size; int face; } fonts[64];
static struct { TTF_Text* text; TTF_Font* font; int width; } runs[128];
static int font_count;
static bool initialized;
static TTF_TextEngine engine;
int otui_text_resources(void) {
  int count = font_count;
  for (int i = 0; i < 128; ++i) if (runs[i].text) ++count;
  return count;
}
const char* otui_text_error(void) { return SDL_GetError(); }
static bool create_text(void* userdata, TTF_Text* text) {
  (void)userdata;
  text->internal->engine_text = text;
  return true;
}
static void destroy_text(void* userdata, TTF_Text* text) {
  (void)userdata; text->internal->engine_text = NULL;
}
void otui_text_clear(void) {
  for (int i = 0; i < 128; ++i) {
    if (runs[i].text) TTF_DestroyText(runs[i].text);
    memset(&runs[i], 0, sizeof(runs[i]));
  }
  for (int i = 0; i < font_count; ++i) TTF_CloseFont(fonts[i].font);
  font_count = 0;
  if (initialized) TTF_Quit();
  initialized = false;
}
TTF_Text* otui_text_get(const char* string, int face, float size, float width, float scale) {
  if (!string || face < 0 || face > 1 || !isfinite(size) || size < 1 || size > 256 ||
      !isfinite(width) || width > 1048576 || !isfinite(scale) || scale < 1 || scale > 4) {
    SDL_SetError("Invalid UI text options"); return NULL;
  }
  size *= scale;
  if (width >= 0) width *= scale;
  if (!initialized) {
    if (!TTF_Init()) return NULL;
    initialized = true;
    SDL_INIT_INTERFACE(&engine);
    engine.CreateText = create_text; engine.DestroyText = destroy_text;
  }
  TTF_Font* font = NULL;
  for (int i = 0; i < font_count; ++i)
    if (fonts[i].face == face && fonts[i].size == size) font = fonts[i].font;
  if (!font) {
    if (font_count == 64) { SDL_SetError("UI font size limit reached (64)"); return NULL; }
    font = TTF_OpenFont(face ? OTUI_FONT_DIR "/JetBrainsMono.ttf" : OTUI_FONT_DIR "/NotoSans.ttf", size);
    if (!font) return NULL;
    fonts[font_count].font = font; fonts[font_count].face = face;
    fonts[font_count++].size = size;
  }
  // SDL_ttf uses zero for newline-only wrapping. A zero-width Yoga constraint
  // must still wrap, so use the narrowest representable width for shaping.
  int wrap = width < 0 ? 0 : (int)fmaxf(1, floorf(width));
  Uint32 hash = 2166136261u;
  for (const unsigned char* p = (const unsigned char*)string; *p; ++p) hash = (hash ^ *p) * 16777619u;
  unsigned slot = (hash ^ (Uint32)wrap ^ (Uint32)(size * 64) ^ (Uint32)face) % 128;
  if (runs[slot].text && runs[slot].font == font && runs[slot].width == wrap &&
      strcmp(runs[slot].text->text, string) == 0) return runs[slot].text;
  TTF_Text* text = TTF_CreateText(&engine, font, string, 0);
  if (!text) return NULL;
  if (!TTF_SetTextWrapWidth(text, wrap) || !TTF_UpdateText(text)) {
    TTF_DestroyText(text); return NULL;
  }
  if (runs[slot].text) TTF_DestroyText(runs[slot].text);
  runs[slot].text = text; runs[slot].font = font; runs[slot].width = wrap;
  return text;
}
float otui_text_size(TTF_Text* text, int field) {
  return field ? text->internal->h : text->internal->w;
}
