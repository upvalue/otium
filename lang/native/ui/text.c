#include "text.h"
#include <math.h>
#include <limits.h>
#include <string.h>

// Fonts stay alive while any renderer can reference their glyphs. Shaped runs
// have a bounded direct-mapped cache; atlas entries don't reference these runs.
static struct {
  TTF_Font* font;
  float size;
  int face;
} fonts[64];

static struct {
  TTF_Text* text;
  TTF_Font* font;
  int width;
} runs[128];

static char* font_paths[32];
static int path_count;
static int font_count;
static bool initialized;
static TTF_TextEngine engine;

int otui_text_resources(void) {
  int count = font_count + path_count;
  for (int i = 0; i < 128; ++i)
    if (runs[i].text) ++count;
  return count;
}

const char* otui_text_error(void) { return SDL_GetError(); }

static bool create_text(void* userdata, TTF_Text* text) {
  (void)userdata;
  text->internal->engine_text = text;
  return true;
}

static void destroy_text(void* userdata, TTF_Text* text) {
  (void)userdata;
  text->internal->engine_text = NULL;
}

static bool init_text(void) {
  if (initialized) return true;
  if (!TTF_Init()) return false;
  initialized = true;
  SDL_INIT_INTERFACE(&engine);
  engine.CreateText = create_text;
  engine.DestroyText = destroy_text;
  return true;
}

int otui_font_register(const char* path) {
  if (!path || !*path) {
    SDL_SetError("Font path must not be empty");
    return -1;
  }
  for (int i = 0; i < path_count; ++i)
    if (strcmp(font_paths[i], path) == 0) return i + 2;
  if (path_count == 32) {
    SDL_SetError("UI font file limit reached (32)");
    return -1;
  }
  if (!init_text()) return -1;
  // Validate before publishing an ID. Failed loads consume no registry slots.
  TTF_Font* probe = TTF_OpenFont(path, 14);
  if (!probe) return -1;
  TTF_CloseFont(probe);
  char* copy = SDL_strdup(path);
  if (!copy) return -1;
  font_paths[path_count++] = copy;
  return path_count + 1;
}

void otui_text_clear(void) {
  for (int i = 0; i < 128; ++i) {
    if (runs[i].text) TTF_DestroyText(runs[i].text);
    memset(&runs[i], 0, sizeof(runs[i]));
  }
  for (int i = 0; i < font_count; ++i) TTF_CloseFont(fonts[i].font);
  font_count = 0;
  for (int i = 0; i < path_count; ++i) SDL_free(font_paths[i]);
  memset(font_paths, 0, sizeof(font_paths));
  path_count = 0;
  if (initialized) TTF_Quit();
  initialized = false;
}

TTF_Text* otui_text_get(const char* string, int face, float size, float width, float scale) {
  if (!string || face < 0 || face >= path_count + 2 || !isfinite(size) || size < 1 || size > 256 ||
      !isfinite(width) || width > 1048576 || !isfinite(scale) || scale < 1 || scale > 4) {
    SDL_SetError("Invalid UI text options");
    return NULL;
  }
  size *= scale;
  if (width >= 0) width *= scale;
  if (!init_text()) return NULL;
  TTF_Font* font = NULL;
  for (int i = 0; i < font_count; ++i)
    if (fonts[i].face == face && fonts[i].size == size) font = fonts[i].font;
  if (!font) {
    if (font_count == 64) {
      SDL_SetError("UI font size limit reached (64)");
      return NULL;
    }
    const char* path = face >= 2 ? font_paths[face - 2]
                       : face    ? OTUI_FONT_DIR "/JetBrainsMono.ttf"
                                 : OTUI_FONT_DIR "/NotoSans.ttf";
    font = TTF_OpenFont(path, size);
    if (!font) return NULL;
    fonts[font_count].font = font;
    fonts[font_count].face = face;
    fonts[font_count++].size = size;
  }
  // SDL_ttf uses zero for newline-only wrapping. A zero-width Yoga constraint
  // must still wrap, so use the narrowest representable width for shaping.
  int wrap = width < 0 ? 0 : (int)fmaxf(1, floorf(width));
  Uint32 hash = 2166136261u;
  for (const unsigned char* p = (const unsigned char*)string; *p; ++p)
    hash = (hash ^ *p) * 16777619u;
  unsigned slot = (hash ^ (Uint32)wrap ^ (Uint32)(size * 64) ^ (Uint32)face) % 128;
  if (runs[slot].text && runs[slot].font == font && runs[slot].width == wrap &&
      strcmp(runs[slot].text->text ? runs[slot].text->text : "", string) == 0)
    return runs[slot].text;
  TTF_Text* text = TTF_CreateText(&engine, font, string, 0);
  if (!text) return NULL;
  // Editable, unwrapped lines need trailing spaces to retain their advances.
  // Otherwise SDL_ttf collapses them into a zero-width end-of-line cluster.
  if (!TTF_SetTextWrapWidth(text, wrap) || !TTF_SetTextWrapWhitespaceVisible(text, width < 0) ||
      !TTF_UpdateText(text)) {
    TTF_DestroyText(text);
    return NULL;
  }
  if (runs[slot].text) TTF_DestroyText(runs[slot].text);
  runs[slot].text = text;
  runs[slot].font = font;
  runs[slot].width = wrap;
  return text;
}

float otui_text_size(TTF_Text* text, int field) {
  return field ? text->internal->h : text->internal->w;
}

bool otui_text_carets(TTF_Text* text, float* positions, int capacity) {
  if (!text || !positions || capacity < 1 || text->num_lines > 1)
    return SDL_SetError("Caret positions require a single line and an output buffer");
  const char* string = text->text ? text->text : "";
  size_t bytes = strlen(string);
  if (bytes > INT_MAX) return SDL_SetError("Text exceeds caret index range");
  if (!bytes) {
    positions[0] = 0;
    return true;
  }

  // SDL_ttf offsets are UTF-8 bytes; Otium indexes strings by codepoint.
  int* indices = SDL_calloc(bytes + 1, sizeof(*indices));
  if (!indices) return false;
  int codepoints = 0;
  for (size_t i = 0; i < bytes; ++i) {
    indices[i] = codepoints;
    if (((unsigned char)string[i] & 0xc0) != 0x80) ++codepoints;
  }
  indices[bytes] = codepoints;
  if (capacity <= codepoints) {
    SDL_free(indices);
    return SDL_SetError("Caret output buffer is too small");
  }
  TTF_SubString current;
  bool ok = TTF_GetTextSubString(text, 0, &current);
  for (int i = 0; i <= codepoints; ++i) positions[i] = 0;
  while (ok && !(current.flags & TTF_SUBSTRING_TEXT_END)) {
    TTF_SubString* cluster = &current;
    if (cluster->offset < 0 || cluster->length < 0 || cluster->offset > (int)bytes ||
        cluster->length > (int)bytes - cluster->offset) {
      ok = SDL_SetError("Invalid shaped text cluster range");
      break;
    }
    int first = indices[cluster->offset];
    int last = indices[cluster->offset + cluster->length];
    bool rtl = (cluster->flags & TTF_SUBSTRING_DIRECTION_MASK) == TTF_DIRECTION_RTL;
    // Preserve codepoint editing inside ligatures using the shaped cluster's span.
    // This is interpolation, not font-provided ligature caret data.
    for (int j = first; j < last; ++j) {
      float fraction = (float)(j - first) / (last - first);
      positions[j] = cluster->rect.x + cluster->rect.w * (rtl ? 1 - fraction : fraction);
    }
    TTF_SubString next;
    ok = TTF_GetNextTextSubString(text, &current, &next);
    if (ok) current = next;
  }
  TTF_SubString end;
  if (ok) {
    ok = TTF_GetTextSubString(text, (int)bytes, &end);
    if (ok) positions[codepoints] = end.rect.x;
  }
  SDL_free(indices);
  return ok;
}
