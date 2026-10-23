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
 * At most 32 custom files and 64 total face/size pairs can be retained. */
int otui_font_register(const char* path);
TTF_Text* otui_text_get(const char* string, int face, float size, float width, float scale);
float otui_text_size(TTF_Text* text, int field);
/* Copies physical-pixel caret positions into caller-owned storage. Requires
 * a single line and capacity >= codepoint count + 1. No pointers are retained. */
bool otui_text_carets(TTF_Text* text, float* positions, int capacity);
void otui_text_clear(void);
