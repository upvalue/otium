#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdlib.h>

// Independent surface renderer oracle for the atlas at 1x and 2x density.
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  int scale = atoi(argv[1]);
  if (scale < 1 || scale > 2 || !TTF_Init()) return 2;
  TTF_Font* font = TTF_OpenFont(OTUI_FONT_DIR "/NotoSans.ttf", 20 * scale);
  TTF_TextEngine* engine = TTF_CreateSurfaceTextEngine();
  TTF_Text* text = font && engine ? TTF_CreateText(engine, font, "office café λ\nwrapped words", 0) : NULL;
  SDL_Surface* out = SDL_CreateSurface(160*scale, 120*scale, SDL_PIXELFORMAT_RGBA32);
  bool ok = text && out && TTF_SetTextWrapWidth(text, 140*scale);
  if (ok) ok = SDL_FillSurfaceRect(out, NULL, SDL_MapSurfaceRGBA(out, 0, 0, 0, 255)) &&
               TTF_DrawSurfaceText(text, 10*scale, 10*scale, out) && SDL_SaveBMP(out, argv[2]);
  if (!ok) SDL_Log("%s", SDL_GetError());
  SDL_DestroySurface(out);
  TTF_DestroyText(text);
  TTF_DestroySurfaceTextEngine(engine);
  TTF_CloseFont(font);
  TTF_Quit();
  return ok ? 0 : 1;
}
