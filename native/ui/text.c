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
} fonts[256];

static struct {
  TTF_Text* text;
  TTF_Font* font;
  int width;
} runs[128];

// A registered face: a font file, the face within it, styles SDL_ttf should
// synthesize, hinting, and fonts to take missing glyphs from. IDs are index + 2.
typedef struct {
  char* path;
  int index, style, hinting, fallback_count;
  int fallbacks[8];
} Face;

static Face faces[64];
static int face_count;
static int font_count;
static bool initialized;
static TTF_TextEngine engine;

static void clear_cells(void);

int otui_text_resources(void) {
  int count = font_count + face_count;
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

const char* otui_font_builtin(int face) {
  return face == 1 ? OTUI_FONT_DIR "/JetBrainsMono.ttf"
       : face == 0 ? OTUI_FONT_DIR "/NotoSans.ttf"
                   : NULL;
}

static TTF_Font* open_file(const char* path, int index, float size) {
  SDL_PropertiesID props = SDL_CreateProperties();
  if (!props) return NULL;
  SDL_SetStringProperty(props, TTF_PROP_FONT_CREATE_FILENAME_STRING, path);
  SDL_SetNumberProperty(props, TTF_PROP_FONT_CREATE_FACE_NUMBER, index);
  SDL_SetFloatProperty(props, TTF_PROP_FONT_CREATE_SIZE_FLOAT, size);
  TTF_Font* font = TTF_OpenFontWithProperties(props);
  SDL_DestroyProperties(props);
  return font;
}

int otui_font_face(const char* path, int index, int style, int hinting, const int* fallbacks,
                   int fallback_count) {
  if (!path || !*path) {
    SDL_SetError("Font path must not be empty");
    return -1;
  }
  if (index < 0 || (style & ~(TTF_STYLE_BOLD | TTF_STYLE_ITALIC)) || hinting < -1 ||
      hinting > TTF_HINTING_LIGHT_SUBPIXEL || fallback_count < 0 || fallback_count > 8 ||
      (fallback_count && !fallbacks)) {
    SDL_SetError("Invalid font face options");
    return -1;
  }
  // Fallbacks must already exist, so they can't form a cycle.
  for (int i = 0; i < fallback_count; ++i)
    if (fallbacks[i] < 0 || fallbacks[i] >= face_count + 2) {
      SDL_SetError("Unknown fallback font face");
      return -1;
    }
  for (int i = 0; i < face_count; ++i) {
    Face* f = &faces[i];
    if (f->index == index && f->style == style && f->hinting == hinting &&
        f->fallback_count == fallback_count && strcmp(f->path, path) == 0 &&
        memcmp(f->fallbacks, fallbacks ? fallbacks : f->fallbacks,
               (size_t)fallback_count * sizeof(int)) == 0)
      return i + 2;
  }
  if (face_count == 64) {
    SDL_SetError("UI font face limit reached (64)");
    return -1;
  }
  if (!init_text()) return -1;
  // Validate before publishing an ID. Failed loads consume no registry slots.
  TTF_Font* probe = open_file(path, index, 14);
  if (!probe) return -1;
  TTF_CloseFont(probe);
  char* copy = SDL_strdup(path);
  if (!copy) return -1;
  Face* f = &faces[face_count++];
  *f = (Face){copy, index, style, hinting, fallback_count, {0}};
  if (fallback_count) memcpy(f->fallbacks, fallbacks, (size_t)fallback_count * sizeof(int));
  return face_count + 1;
}

int otui_font_register(const char* path) { return otui_font_face(path, 0, 0, -1, NULL, 0); }

int otui_font_describe(const char* path, int index, char* family, int family_cap, char* style,
                       int style_cap) {
  if (!path || index < 0 || !family || !style || family_cap < 1 || style_cap < 1) {
    SDL_SetError("Invalid font description request");
    return -1;
  }
  if (!init_text()) return -1;
  TTF_Font* font = open_file(path, index, 12);
  if (!font) return -1;
  const char* name = TTF_GetFontFamilyName(font);
  const char* kind = TTF_GetFontStyleName(font);
  SDL_strlcpy(family, name ? name : "", (size_t)family_cap);
  SDL_strlcpy(style, kind ? kind : "", (size_t)style_cap);
  int count = TTF_GetNumFontFaces(font);
  TTF_CloseFont(font);
  return count;
}

void otui_text_clear(void) {
  clear_cells();
  for (int i = 0; i < 128; ++i) {
    if (runs[i].text) TTF_DestroyText(runs[i].text);
    memset(&runs[i], 0, sizeof(runs[i]));
  }
  for (int i = 0; i < font_count; ++i) TTF_CloseFont(fonts[i].font);
  font_count = 0;
  for (int i = 0; i < face_count; ++i) SDL_free(faces[i].path);
  memset(faces, 0, sizeof(faces));
  face_count = 0;
  if (initialized) TTF_Quit();
  initialized = false;
}

// size is in physical pixels.
static TTF_Font* face_font(int face, float size) {
  for (int i = 0; i < font_count; ++i)
    if (fonts[i].face == face && fonts[i].size == size) return fonts[i].font;
  if (font_count == 256) {
    SDL_SetError("UI font size limit reached (256)");
    return NULL;
  }
  Face* f = face >= 2 ? &faces[face - 2] : NULL;
  TTF_Font* font = f ? open_file(f->path, f->index, size) : TTF_OpenFont(otui_font_builtin(face), size);
  if (!font) return NULL;
  if (f) {
    if (f->style) TTF_SetFontStyle(font, f->style);
    if (f->hinting >= 0) TTF_SetFontHinting(font, f->hinting);
  }
  fonts[font_count].font = font;
  fonts[font_count].face = face;
  fonts[font_count++].size = size;
  for (int i = 0; f && i < f->fallback_count; ++i) {
    TTF_Font* fallback = face_font(f->fallbacks[i], size);
    if (!fallback || !TTF_AddFallbackFont(font, fallback)) return NULL;
  }
  return font;
}

static bool valid_face(int face, float size, float scale) {
  if (face < 0 || face >= face_count + 2 || !isfinite(size) || size < 1 || size > 256 ||
      !isfinite(scale) || scale < 1 || scale > 4)
    return SDL_SetError("Invalid UI text options");
  return init_text();
}

bool otui_font_metrics(int face, float size, float scale, float* out) {
  if (!out || !valid_face(face, size, scale)) return false;
  TTF_Font* font = face_font(face, size * scale);
  if (!font) return false;
  int advance = 0;
  if (!TTF_GetGlyphMetrics(font, 'M', NULL, NULL, NULL, NULL, &advance)) return false;
  out[0] = (float)advance;
  out[1] = (float)TTF_GetFontHeight(font);
  out[2] = (float)TTF_GetFontAscent(font);
  out[3] = (float)TTF_GetFontDescent(font);
  return true;
}

TTF_Text* otui_text_get(const char* string, int face, float size, float width, float scale) {
  if (!string || !isfinite(width) || width > 1048576 || !valid_face(face, size, scale)) {
    SDL_SetError("Invalid UI text options");
    return NULL;
  }
  size *= scale;
  if (width >= 0) width *= scale;
  TTF_Font* font = face_font(face, size);
  if (!font) return NULL;
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

// Terminal cells: one grapheme shaped on its own, so it lands in its column
// whatever the font's advances or ligatures. Shapes are cached by face, size
// and bytes; the table empties when it fills, as glyphs live in the atlas.
#define CELL_SLOTS 8192
#define CELL_OPS 32768
#define CELL_BYTES 31

typedef struct {
  int face;
  float size;
  Uint8 len;
  char bytes[CELL_BYTES];
  Uint16 count;
  Uint32 first;
} CellEntry;

static CellEntry cells[CELL_SLOTS];
static OtuiCellOp cell_ops[CELL_OPS];
static int cell_used, cell_op_count;
static OtuiCellOp scratch_ops[64];

static void clear_cells(void) {
  memset(cells, 0, sizeof(cells));
  cell_used = cell_op_count = 0;
}

static int shape_cell(TTF_Font* font, const char* bytes, int len, OtuiCellOp* out, int cap) {
  TTF_Text* text = TTF_CreateText(&engine, font, bytes, (size_t)len);
  if (!text) return -1;
  if (!TTF_UpdateText(text)) {
    TTF_DestroyText(text);
    return -1;
  }
  int n = 0;
  for (int i = 0; i < text->internal->num_ops && n < cap; ++i) {
    TTF_DrawOperation* op = &text->internal->ops[i];
    if (op->cmd != TTF_DRAW_COMMAND_COPY) continue;
    out[n++] = (OtuiCellOp){op->copy.glyph_font, op->copy.glyph_index, op->copy.src, op->copy.dst};
  }
  TTF_DestroyText(text);
  return n;
}

int otui_text_cell(const char* bytes, int len, int face, float size, float scale,
                   const OtuiCellOp** ops) {
  if (!bytes || len < 1 || !ops || !valid_face(face, size, scale)) return -1;
  size *= scale;
  TTF_Font* font = face_font(face, size);
  if (!font) return -1;
  if (len > CELL_BYTES) {
    *ops = scratch_ops;
    return shape_cell(font, bytes, len, scratch_ops, 64);
  }
  Uint32 hash = 2166136261u ^ (Uint32)face ^ (Uint32)(size * 64);
  for (int i = 0; i < len; ++i) hash = (hash ^ (Uint8)bytes[i]) * 16777619u;
  for (Uint32 probe = 0;; ++probe) {
    CellEntry* e = &cells[(hash + probe) % CELL_SLOTS];
    if (!e->len) break;
    if (e->face == face && e->size == size && e->len == len && memcmp(e->bytes, bytes, (size_t)len) == 0) {
      *ops = &cell_ops[e->first];
      return e->count;
    }
  }
  if (cell_used >= CELL_SLOTS * 3 / 4 || cell_op_count > CELL_OPS - 64) clear_cells();
  OtuiCellOp* out = &cell_ops[cell_op_count];
  int n = shape_cell(font, bytes, len, out, 64);
  if (n < 0) return -1;
  Uint32 slot = hash % CELL_SLOTS;
  while (cells[slot].len) slot = (slot + 1) % CELL_SLOTS;
  CellEntry* e = &cells[slot];
  e->face = face;
  e->size = size;
  e->len = (Uint8)len;
  memcpy(e->bytes, bytes, (size_t)len);
  e->count = (Uint16)n;
  e->first = (Uint32)cell_op_count;
  cell_op_count += n;
  ++cell_used;
  *ops = out;
  return n;
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
