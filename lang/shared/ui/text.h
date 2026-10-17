#pragma once
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_ttf/SDL_textengine.h>
TTF_Text* otui_text_get(const char* string, int face, float size, float width, float scale);
float otui_text_size(TTF_Text* text, int field);
void otui_text_clear(void);
