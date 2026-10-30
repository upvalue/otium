#pragma once
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_ttf/SDL_textengine.h>
/* Main-thread SDL_ttf bridge. Returned text is borrowed from the bounded
 * cache and may be evicted by a later get or invalidated by clear. Consume it
 * before another get; never destroy it in the caller. NULL/false report errors
 * through SDL_GetError. clear destroys runs before fonts and permits reuse.
 * size/width are logical pixels; scale converts them to physical pixels. */
/* Register a font file (first face of a collection). Returns a face ID >= 2,
 * or -1 on error. Paths are copied and deduplicated; IDs last until clear.
 * At most 64 faces and 256 face/size pairs can be retained. */
int otui_font_register(const char* path);
/* Register face index of a file, with TTF_STYLE_BOLD/ITALIC to synthesize,
 * TTF_HINTING_* or -1 for the default, and up to 8 earlier face IDs to take
 * missing glyphs from. Identical registrations share an ID. */
int otui_font_face(const char* path, int index, int style, int hinting, const int* fallbacks,
                   int fallback_count);
/* The file behind built-in face 0 (UI) or 1 (mono), or NULL. */
const char* otui_font_builtin(int face);
/* Copy the family and style names of face index of a file. Returns the number
 * of faces in the file, or -1. */
int otui_font_describe(const char* path, int index, char* family, int family_cap, char* style,
                       int style_cap);
/* Physical-pixel advance of M, height, ascent and descent. */
bool otui_font_metrics(int face, float size, float scale, float* out);

/* One glyph of a shaped terminal cell: its atlas source and its position
 * relative to the cell's text origin, in physical pixels. */
typedef struct {
  TTF_Font* font;
  Uint32 index;
  SDL_Rect src, dst;
} OtuiCellOp;
/* Shape one cell's grapheme. Returns the op count and points ops at them, or
 * -1. The ops are borrowed until the next call. */
int otui_text_cell(const char* bytes, int len, int face, float size, float scale,
                   const OtuiCellOp** ops);
TTF_Text* otui_text_get(const char* string, int face, float size, float width, float scale);
float otui_text_size(TTF_Text* text, int field);
/* Copies physical-pixel caret positions into caller-owned storage. Requires
 * a single line and capacity >= codepoint count + 1. No pointers are retained. */
bool otui_text_carets(TTF_Text* text, float* positions, int capacity);
void otui_text_clear(void);
