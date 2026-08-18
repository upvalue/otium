#define OT_INTERNAL
#include "otium.h"

#include <raylib.h>
#include <rlgl.h>
#include <stdlib.h>
#include <string.h>

typedef struct ray_font {
  Font value;
  bool owned;
} ray_font;

static bool need_count(ots* state, const char* who, int argc, int expected) {
  if (argc == expected) return true;
  ot_raise(state, "%s: expected %d argument%s", who, expected, expected == 1 ? "" : "s");
  return false;
}

static bool need_int(ots* state, const char* who, otv value) {
  if (ot_is_int(value)) return true;
  ot_raise(state, "%s: expected int", who);
  return false;
}

static bool need_number(ots* state, const char* who, otv value) {
  if (ot_is_int(value) || ot_has_type(value, OBJ_FLOAT)) return true;
  ot_raise(state, "%s: expected number", who);
  return false;
}

static bool need_string(ots* state, const char* who, otv value) {
  if (ot_has_type(value, OBJ_STRING)) return true;
  ot_raise(state, "%s: expected string", who);
  return false;
}

static bool need_window(ots* state, const char* who) {
  if (IsWindowReady()) return true;
  ot_raise(state, "%s: window is not initialized", who);
  return false;
}

static double number_value(otv value) {
  if (ot_is_int(value)) return (double)ot_get_int(value);
  return ((ot_float_obj*)ot_as_obj(value))->value;
}

static Color color_value(otv value) {
  uint32_t rgba = (uint32_t)ot_get_int(value);
  return (Color){(unsigned char)(rgba >> 24), (unsigned char)(rgba >> 16),
                 (unsigned char)(rgba >> 8), (unsigned char)rgba};
}

static char* copy_cstr(ots* state, const char* who, otv value) {
  if (!need_string(state, who, value)) return NULL;
  const char* bytes;
  size_t length;
  (void)ot_string_bytes(value, &bytes, &length);
  char* copy = ot_host_alloc(length + 1);
  if (copy == NULL) {
    ot_raise(state, "%s: out of memory", who);
    return NULL;
  }
  memcpy(copy, bytes, length);
  copy[length] = '\0';
  return copy;
}

static void finish_texture(ots* state, void* payload) {
  (void)state;
  Texture2D* texture = payload;
  if (IsWindowReady() && texture->id != 0) UnloadTexture(*texture);
  ot_host_free(payload);
}

static void finish_font(ots* state, void* payload) {
  (void)state;
  ray_font* font = payload;
  if (font->owned && IsWindowReady() && font->value.texture.id != 0) UnloadFont(font->value);
  ot_host_free(payload);
}

static void finish_render_texture(ots* state, void* payload) {
  (void)state;
  RenderTexture2D* target = payload;
  if (IsWindowReady() && target->id != 0) UnloadRenderTexture(*target);
  ot_host_free(payload);
}

static unsigned texture_type(ots* state) {
  return ot_ext_type(state, "ray/texture", finish_texture);
}

static unsigned font_type(ots* state) { return ot_ext_type(state, "ray/font", finish_font); }

static unsigned render_texture_type(ots* state) {
  return ot_ext_type(state, "ray/render-texture", finish_render_texture);
}

static otv make_texture(ots* state, Texture2D value) {
  Texture2D* payload = ot_host_alloc(sizeof(*payload));
  if (payload == NULL) return ot_raise(state, "load-texture: out of memory");
  *payload = value;
  return ot_ext_pointer(state, texture_type(state), payload);
}

static otv make_font(ots* state, Font value, bool owned) {
  ray_font* payload = ot_host_alloc(sizeof(*payload));
  if (payload == NULL) return ot_raise(state, "load-font: out of memory");
  *payload = (ray_font){.value = value, .owned = owned};
  return ot_ext_pointer(state, font_type(state), payload);
}

static otv make_render_texture(ots* state, RenderTexture2D value) {
  RenderTexture2D* payload = ot_host_alloc(sizeof(*payload));
  if (payload == NULL) return ot_raise(state, "load-render-texture: out of memory");
  *payload = value;
  return ot_ext_pointer(state, render_texture_type(state), payload);
}

static bool texture_payload(ots* state, const char* who, otv value, Texture2D** out) {
  return ot_ext_check(state, who, value, texture_type(state), (void**)out);
}

static bool font_payload(ots* state, const char* who, otv value, Font** out) {
  ray_font* font;
  if (!ot_ext_check(state, who, value, font_type(state), (void**)&font)) return false;
  *out = &font->value;
  return true;
}

static bool render_texture_payload(ots* state, const char* who, otv value, RenderTexture2D** out) {
  return ot_ext_check(state, who, value, render_texture_type(state), (void**)out);
}

static otv ray_init_window(ots* state, otv* args, int argc) {
  const char* who = "init-window";
  if (!need_count(state, who, argc, 3) || !need_int(state, who, args[0]) ||
      !need_int(state, who, args[1]))
    return OT_UNWIND;
  if (IsWindowReady()) return ot_raise(state, "%s: window is already initialized", who);
  char* title = copy_cstr(state, who, args[2]);
  if (title == NULL) return OT_UNWIND;
  InitWindow((int)ot_get_int(args[0]), (int)ot_get_int(args[1]), title);
  ot_host_free(title);
  if (!IsWindowReady()) return ot_raise(state, "%s: Raylib could not create the window", who);
  return ot_nil;
}

static otv ray_close_window(ots* state, otv* args, int argc) {
  (void)args;
  if (!need_count(state, "close-window", argc, 0)) return OT_UNWIND;
  if (IsWindowReady()) CloseWindow();
  return ot_nil;
}

static otv ray_window_ready(ots* state, otv* args, int argc) {
  (void)args;
  if (!need_count(state, "window-ready?", argc, 0)) return OT_UNWIND;
  return IsWindowReady() ? ot_true : ot_false;
}

static otv ray_window_should_close(ots* state, otv* args, int argc) {
  (void)args;
  const char* who = "window-should-close?";
  if (!need_count(state, who, argc, 0) || !need_window(state, who)) return OT_UNWIND;
  return WindowShouldClose() ? ot_true : ot_false;
}

static otv ray_set_config_flags(ots* state, otv* args, int argc) {
  const char* who = "set-config-flags!";
  if (!need_count(state, who, argc, 1) || !need_int(state, who, args[0])) return OT_UNWIND;
  if (IsWindowReady()) return ot_raise(state, "%s: call this before init-window", who);
  SetConfigFlags((unsigned)ot_get_int(args[0]));
  return ot_nil;
}

static otv ray_window_resized(ots* state, otv* args, int argc) {
  (void)args;
  const char* who = "window-resized?";
  if (!need_count(state, who, argc, 0) || !need_window(state, who)) return OT_UNWIND;
  return IsWindowResized() ? ot_true : ot_false;
}

static otv ray_window_fullscreen(ots* state, otv* args, int argc) {
  (void)args;
  const char* who = "window-fullscreen?";
  if (!need_count(state, who, argc, 0) || !need_window(state, who)) return OT_UNWIND;
  return IsWindowFullscreen() ? ot_true : ot_false;
}

static otv ray_toggle_fullscreen(ots* state, otv* args, int argc) {
  (void)args;
  const char* who = "toggle-fullscreen!";
  if (!need_count(state, who, argc, 0) || !need_window(state, who)) return OT_UNWIND;
  ToggleFullscreen();
  return ot_nil;
}

static otv ray_set_window_size(ots* state, otv* args, int argc) {
  const char* who = "set-window-size!";
  if (!need_count(state, who, argc, 2) || !need_int(state, who, args[0]) ||
      !need_int(state, who, args[1]) || !need_window(state, who))
    return OT_UNWIND;
  SetWindowSize((int)ot_get_int(args[0]), (int)ot_get_int(args[1]));
  return ot_nil;
}

static otv ray_set_window_title(ots* state, otv* args, int argc) {
  const char* who = "set-window-title!";
  if (!need_count(state, who, argc, 1) || !need_window(state, who)) return OT_UNWIND;
  char* title = copy_cstr(state, who, args[0]);
  if (title == NULL) return OT_UNWIND;
  SetWindowTitle(title);
  ot_host_free(title);
  return ot_nil;
}

static otv ray_set_target_fps(ots* state, otv* args, int argc) {
  const char* who = "set-target-fps!";
  if (!need_count(state, who, argc, 1) || !need_int(state, who, args[0])) return OT_UNWIND;
  SetTargetFPS((int)ot_get_int(args[0]));
  return ot_nil;
}

static otv ray_frame_time(ots* state, otv* args, int argc) {
  (void)args;
  if (!need_count(state, "frame-time", argc, 0)) return OT_UNWIND;
  return ot_make_float(state, GetFrameTime());
}

static otv ray_time(ots* state, otv* args, int argc) {
  (void)args;
  if (!need_count(state, "time", argc, 0) || !need_window(state, "time")) return OT_UNWIND;
  return ot_make_float(state, GetTime());
}

static otv ray_fps(ots* state, otv* args, int argc) {
  (void)args;
  if (!need_count(state, "fps", argc, 0) || !need_window(state, "fps")) return OT_UNWIND;
  return ot_make_int(GetFPS());
}

static otv ray_set_seed(ots* state, otv* args, int argc) {
  const char* who = "set-random-seed!";
  if (!need_count(state, who, argc, 1) || !need_int(state, who, args[0])) return OT_UNWIND;
  SetRandomSeed((unsigned)ot_get_int(args[0]));
  return ot_nil;
}

static otv ray_random(ots* state, otv* args, int argc) {
  const char* who = "random-value";
  if (!need_count(state, who, argc, 2) || !need_int(state, who, args[0]) ||
      !need_int(state, who, args[1]))
    return OT_UNWIND;
  if (ot_get_int(args[0]) > ot_get_int(args[1]))
    return ot_raise(state, "%s: minimum exceeds maximum", who);
  return ot_make_int(GetRandomValue((int)ot_get_int(args[0]), (int)ot_get_int(args[1])));
}

static otv ray_screen_width(ots* state, otv* args, int argc) {
  (void)args;
  if (!need_count(state, "screen-width", argc, 0) || !need_window(state, "screen-width"))
    return OT_UNWIND;
  return ot_make_int(GetScreenWidth());
}

static otv ray_screen_height(ots* state, otv* args, int argc) {
  (void)args;
  if (!need_count(state, "screen-height", argc, 0) || !need_window(state, "screen-height"))
    return OT_UNWIND;
  return ot_make_int(GetScreenHeight());
}

static otv ray_begin_drawing(ots* state, otv* args, int argc) {
  (void)args;
  if (!need_count(state, "begin-drawing", argc, 0) || !need_window(state, "begin-drawing"))
    return OT_UNWIND;
  BeginDrawing();
  return ot_nil;
}

static otv ray_end_drawing(ots* state, otv* args, int argc) {
  (void)args;
  if (!need_count(state, "end-drawing", argc, 0) || !need_window(state, "end-drawing"))
    return OT_UNWIND;
  EndDrawing();
  return ot_nil;
}

static otv ray_clear_background(ots* state, otv* args, int argc) {
  const char* who = "clear-background";
  if (!need_count(state, who, argc, 1) || !need_int(state, who, args[0]) ||
      !need_window(state, who))
    return OT_UNWIND;
  ClearBackground(color_value(args[0]));
  return ot_nil;
}

static otv ray_draw_text(ots* state, otv* args, int argc) {
  const char* who = "draw-text";
  if (!need_count(state, who, argc, 5)) return OT_UNWIND;
  for (int i = 1; i < 5; i++)
    if (!need_int(state, who, args[i])) return OT_UNWIND;
  if (!need_window(state, who)) return OT_UNWIND;
  char* text = copy_cstr(state, who, args[0]);
  if (text == NULL) return OT_UNWIND;
  DrawText(text, (int)ot_get_int(args[1]), (int)ot_get_int(args[2]), (int)ot_get_int(args[3]),
           color_value(args[4]));
  ot_host_free(text);
  return ot_nil;
}

static otv ray_draw_rectangle(ots* state, otv* args, int argc) {
  const char* who = "draw-rectangle";
  if (!need_count(state, who, argc, 5)) return OT_UNWIND;
  for (int i = 0; i < 5; i++)
    if (!need_int(state, who, args[i])) return OT_UNWIND;
  if (!need_window(state, who)) return OT_UNWIND;
  DrawRectangle((int)ot_get_int(args[0]), (int)ot_get_int(args[1]), (int)ot_get_int(args[2]),
                (int)ot_get_int(args[3]), color_value(args[4]));
  return ot_nil;
}

static otv ray_draw_circle(ots* state, otv* args, int argc) {
  const char* who = "draw-circle";
  if (!need_count(state, who, argc, 4) || !need_int(state, who, args[0]) ||
      !need_int(state, who, args[1]) || !need_number(state, who, args[2]) ||
      !need_int(state, who, args[3]) || !need_window(state, who))
    return OT_UNWIND;
  DrawCircle((int)ot_get_int(args[0]), (int)ot_get_int(args[1]), (float)number_value(args[2]),
             color_value(args[3]));
  return ot_nil;
}

static otv ray_draw_line(ots* state, otv* args, int argc) {
  const char* who = "draw-line";
  if (!need_count(state, who, argc, 6)) return OT_UNWIND;
  for (int i = 0; i < 5; i++)
    if (!need_number(state, who, args[i])) return OT_UNWIND;
  if (!need_int(state, who, args[5]) || !need_window(state, who)) return OT_UNWIND;
  Vector2 start = {(float)number_value(args[0]), (float)number_value(args[1])};
  Vector2 end = {(float)number_value(args[2]), (float)number_value(args[3])};
  DrawLineEx(start, end, (float)number_value(args[4]), color_value(args[5]));
  return ot_nil;
}

static otv ray_draw_rectangle_lines(ots* state, otv* args, int argc) {
  const char* who = "draw-rectangle-lines";
  if (!need_count(state, who, argc, 6)) return OT_UNWIND;
  for (int i = 0; i < 5; i++)
    if (!need_number(state, who, args[i])) return OT_UNWIND;
  if (!need_int(state, who, args[5]) || !need_window(state, who)) return OT_UNWIND;
  Rectangle rectangle = {(float)number_value(args[0]), (float)number_value(args[1]),
                         (float)number_value(args[2]), (float)number_value(args[3])};
  DrawRectangleLinesEx(rectangle, (float)number_value(args[4]), color_value(args[5]));
  return ot_nil;
}

static otv ray_begin_scissor(ots* state, otv* args, int argc) {
  const char* who = "begin-scissor";
  if (!need_count(state, who, argc, 4)) return OT_UNWIND;
  for (int i = 0; i < 4; i++)
    if (!need_int(state, who, args[i])) return OT_UNWIND;
  if (!need_window(state, who)) return OT_UNWIND;
  BeginScissorMode((int)ot_get_int(args[0]), (int)ot_get_int(args[1]), (int)ot_get_int(args[2]),
                   (int)ot_get_int(args[3]));
  return ot_nil;
}

static otv ray_end_scissor(ots* state, otv* args, int argc) {
  (void)args;
  if (!need_count(state, "end-scissor", argc, 0) || !need_window(state, "end-scissor"))
    return OT_UNWIND;
  EndScissorMode();
  return ot_nil;
}

static otv ray_key_pressed(ots* state, otv* args, int argc) {
  const char* who = "key-pressed?";
  if (!need_count(state, who, argc, 1) || !need_int(state, who, args[0]) ||
      !need_window(state, who))
    return OT_UNWIND;
  return IsKeyPressed((int)ot_get_int(args[0])) ? ot_true : ot_false;
}

static otv ray_key_down(ots* state, otv* args, int argc) {
  const char* who = "key-down?";
  if (!need_count(state, who, argc, 1) || !need_int(state, who, args[0]) ||
      !need_window(state, who))
    return OT_UNWIND;
  return IsKeyDown((int)ot_get_int(args[0])) ? ot_true : ot_false;
}

static otv ray_key_released(ots* state, otv* args, int argc) {
  const char* who = "key-released?";
  if (!need_count(state, who, argc, 1) || !need_int(state, who, args[0]) ||
      !need_window(state, who))
    return OT_UNWIND;
  return IsKeyReleased((int)ot_get_int(args[0])) ? ot_true : ot_false;
}

static otv ray_next_key(ots* state, otv* args, int argc) {
  (void)args;
  if (!need_count(state, "next-key-pressed", argc, 0) || !need_window(state, "next-key-pressed"))
    return OT_UNWIND;
  return ot_make_int(GetKeyPressed());
}

static otv ray_next_char(ots* state, otv* args, int argc) {
  (void)args;
  if (!need_count(state, "next-char-pressed", argc, 0) || !need_window(state, "next-char-pressed"))
    return OT_UNWIND;
  return ot_make_int(GetCharPressed());
}

static otv ray_mouse_x(ots* state, otv* args, int argc) {
  (void)args;
  if (!need_count(state, "mouse-x", argc, 0) || !need_window(state, "mouse-x")) return OT_UNWIND;
  return ot_make_int(GetMouseX());
}

static otv ray_mouse_y(ots* state, otv* args, int argc) {
  (void)args;
  if (!need_count(state, "mouse-y", argc, 0) || !need_window(state, "mouse-y")) return OT_UNWIND;
  return ot_make_int(GetMouseY());
}

static otv ray_mouse_pressed(ots* state, otv* args, int argc) {
  const char* who = "mouse-button-pressed?";
  if (!need_count(state, who, argc, 1) || !need_int(state, who, args[0]) ||
      !need_window(state, who))
    return OT_UNWIND;
  return IsMouseButtonPressed((int)ot_get_int(args[0])) ? ot_true : ot_false;
}

static otv ray_mouse_down(ots* state, otv* args, int argc) {
  const char* who = "mouse-button-down?";
  if (!need_count(state, who, argc, 1) || !need_int(state, who, args[0]) ||
      !need_window(state, who))
    return OT_UNWIND;
  return IsMouseButtonDown((int)ot_get_int(args[0])) ? ot_true : ot_false;
}

static otv ray_mouse_released(ots* state, otv* args, int argc) {
  const char* who = "mouse-button-released?";
  if (!need_count(state, who, argc, 1) || !need_int(state, who, args[0]) ||
      !need_window(state, who))
    return OT_UNWIND;
  return IsMouseButtonReleased((int)ot_get_int(args[0])) ? ot_true : ot_false;
}

static otv ray_mouse_wheel(ots* state, otv* args, int argc) {
  (void)args;
  if (!need_count(state, "mouse-wheel", argc, 0) || !need_window(state, "mouse-wheel"))
    return OT_UNWIND;
  return ot_make_float(state, GetMouseWheelMove());
}

static otv ray_load_texture(ots* state, otv* args, int argc) {
  const char* who = "load-texture";
  if (!need_count(state, who, argc, 1) || !need_window(state, who)) return OT_UNWIND;
  char* path = copy_cstr(state, who, args[0]);
  if (path == NULL) return OT_UNWIND;
  Texture2D texture = LoadTexture(path);
  ot_host_free(path);
  if (texture.id == 0) return ot_raise(state, "%s: Raylib could not load the texture", who);
  return make_texture(state, texture);
}

static otv ray_unload_texture(ots* state, otv* args, int argc) {
  const char* who = "unload-texture!";
  if (!need_count(state, who, argc, 1)) return OT_UNWIND;
  return ot_ext_release(state, who, args[0], texture_type(state));
}

static otv ray_texture_width(ots* state, otv* args, int argc) {
  Texture2D* texture;
  const char* who = "texture-width";
  if (!need_count(state, who, argc, 1) || !texture_payload(state, who, args[0], &texture))
    return OT_UNWIND;
  return ot_make_int(texture->width);
}

static otv ray_texture_height(ots* state, otv* args, int argc) {
  Texture2D* texture;
  const char* who = "texture-height";
  if (!need_count(state, who, argc, 1) || !texture_payload(state, who, args[0], &texture))
    return OT_UNWIND;
  return ot_make_int(texture->height);
}

static otv ray_draw_texture(ots* state, otv* args, int argc) {
  Texture2D* texture;
  const char* who = "draw-texture";
  if (!need_count(state, who, argc, 4) || !need_int(state, who, args[1]) ||
      !need_int(state, who, args[2]) || !need_int(state, who, args[3]) ||
      !need_window(state, who) || !texture_payload(state, who, args[0], &texture))
    return OT_UNWIND;
  DrawTexture(*texture, (int)ot_get_int(args[1]), (int)ot_get_int(args[2]), color_value(args[3]));
  return ot_nil;
}

static otv ray_draw_texture_pro(ots* state, otv* args, int argc) {
  Texture2D* texture;
  const char* who = "draw-texture-pro";
  if (!need_count(state, who, argc, 13)) return OT_UNWIND;
  for (int i = 1; i < 12; i++)
    if (!need_number(state, who, args[i])) return OT_UNWIND;
  if (!need_int(state, who, args[12]) || !need_window(state, who) ||
      !texture_payload(state, who, args[0], &texture))
    return OT_UNWIND;
  Rectangle source = {(float)number_value(args[1]), (float)number_value(args[2]),
                      (float)number_value(args[3]), (float)number_value(args[4])};
  Rectangle destination = {(float)number_value(args[5]), (float)number_value(args[6]),
                           (float)number_value(args[7]), (float)number_value(args[8])};
  Vector2 origin = {(float)number_value(args[9]), (float)number_value(args[10])};
  DrawTexturePro(*texture, source, destination, origin, (float)number_value(args[11]),
                 color_value(args[12]));
  return ot_nil;
}

static bool default_font(Font font) {
  Font builtin = GetFontDefault();
  return font.texture.id == builtin.texture.id && font.recs == builtin.recs &&
         font.glyphs == builtin.glyphs;
}

static otv ray_default_font(ots* state, otv* args, int argc) {
  (void)args;
  const char* who = "default-font";
  if (!need_count(state, who, argc, 0) || !need_window(state, who)) return OT_UNWIND;
  return make_font(state, GetFontDefault(), false);
}

static otv ray_load_font(ots* state, otv* args, int argc) {
  const char* who = "load-font";
  if (!need_count(state, who, argc, 1) || !need_window(state, who)) return OT_UNWIND;
  char* path = copy_cstr(state, who, args[0]);
  if (path == NULL) return OT_UNWIND;
  Font font = LoadFont(path);
  ot_host_free(path);
  if (font.texture.id == 0 || font.glyphCount == 0 || default_font(font))
    return ot_raise(state, "%s: Raylib could not load the font", who);
  return make_font(state, font, true);
}

static otv ray_load_font_ex(ots* state, otv* args, int argc) {
  const char* who = "load-font-ex";
  if (!need_count(state, who, argc, 2) || !need_int(state, who, args[1]) ||
      !need_window(state, who))
    return OT_UNWIND;
  char* path = copy_cstr(state, who, args[0]);
  if (path == NULL) return OT_UNWIND;
  Font font = LoadFontEx(path, (int)ot_get_int(args[1]), NULL, 0);
  ot_host_free(path);
  if (font.texture.id == 0 || font.glyphCount == 0 || default_font(font))
    return ot_raise(state, "%s: Raylib could not load the font", who);
  return make_font(state, font, true);
}

static otv ray_unload_font(ots* state, otv* args, int argc) {
  const char* who = "unload-font!";
  if (!need_count(state, who, argc, 1)) return OT_UNWIND;
  return ot_ext_release(state, who, args[0], font_type(state));
}

static otv ray_font_base_size(ots* state, otv* args, int argc) {
  Font* font;
  const char* who = "font-base-size";
  if (!need_count(state, who, argc, 1) || !font_payload(state, who, args[0], &font))
    return OT_UNWIND;
  return ot_make_int(font->baseSize);
}

static otv ray_draw_font(ots* state, otv* args, int argc) {
  Font* font;
  const char* who = "draw-font";
  if (!need_count(state, who, argc, 7) || !need_window(state, who) ||
      !font_payload(state, who, args[0], &font))
    return OT_UNWIND;
  for (int i = 2; i < 6; i++)
    if (!need_number(state, who, args[i])) return OT_UNWIND;
  if (!need_int(state, who, args[6])) return OT_UNWIND;
  char* text = copy_cstr(state, who, args[1]);
  if (text == NULL) return OT_UNWIND;
  DrawTextEx(*font, text, (Vector2){(float)number_value(args[2]), (float)number_value(args[3])},
             (float)number_value(args[4]), (float)number_value(args[5]), color_value(args[6]));
  ot_host_free(text);
  return ot_nil;
}

static otv ray_draw_codepoint(ots* state, otv* args, int argc) {
  Font* font;
  const char* who = "draw-codepoint";
  if (!need_count(state, who, argc, 6) || !need_int(state, who, args[1]) ||
      !need_number(state, who, args[2]) || !need_number(state, who, args[3]) ||
      !need_number(state, who, args[4]) || !need_int(state, who, args[5]) ||
      !need_window(state, who) || !font_payload(state, who, args[0], &font))
    return OT_UNWIND;
  DrawTextCodepoint(*font, (int)ot_get_int(args[1]),
                    (Vector2){(float)number_value(args[2]), (float)number_value(args[3])},
                    (float)number_value(args[4]), color_value(args[5]));
  return ot_nil;
}

static otv ray_measure_font(ots* state, otv* args, int argc, bool width) {
  Font* font;
  const char* who = width ? "measure-font-width" : "measure-font-height";
  if (!need_count(state, who, argc, 4) || !need_number(state, who, args[2]) ||
      !need_number(state, who, args[3]) || !need_window(state, who) ||
      !font_payload(state, who, args[0], &font))
    return OT_UNWIND;
  char* text = copy_cstr(state, who, args[1]);
  if (text == NULL) return OT_UNWIND;
  Vector2 size =
      MeasureTextEx(*font, text, (float)number_value(args[2]), (float)number_value(args[3]));
  ot_host_free(text);
  return ot_make_float(state, width ? size.x : size.y);
}

static otv ray_measure_font_width(ots* state, otv* args, int argc) {
  return ray_measure_font(state, args, argc, true);
}

static otv ray_measure_font_height(ots* state, otv* args, int argc) {
  return ray_measure_font(state, args, argc, false);
}

static otv ray_load_render_texture(ots* state, otv* args, int argc) {
  const char* who = "load-render-texture";
  if (!need_count(state, who, argc, 2) || !need_int(state, who, args[0]) ||
      !need_int(state, who, args[1]) || !need_window(state, who))
    return OT_UNWIND;
  RenderTexture2D target = LoadRenderTexture((int)ot_get_int(args[0]), (int)ot_get_int(args[1]));
  if (target.id == 0) return ot_raise(state, "%s: Raylib could not create the target", who);
  return make_render_texture(state, target);
}

static otv ray_unload_render_texture(ots* state, otv* args, int argc) {
  const char* who = "unload-render-texture!";
  if (!need_count(state, who, argc, 1)) return OT_UNWIND;
  return ot_ext_release(state, who, args[0], render_texture_type(state));
}

static otv ray_begin_texture_mode(ots* state, otv* args, int argc) {
  RenderTexture2D* target;
  const char* who = "begin-texture-mode";
  if (!need_count(state, who, argc, 1) || !need_window(state, who) ||
      !render_texture_payload(state, who, args[0], &target))
    return OT_UNWIND;
  BeginTextureMode(*target);
  return ot_nil;
}

static otv ray_end_texture_mode(ots* state, otv* args, int argc) {
  (void)args;
  const char* who = "end-texture-mode";
  if (!need_count(state, who, argc, 0) || !need_window(state, who)) return OT_UNWIND;
  EndTextureMode();
  return ot_nil;
}

static otv ray_render_texture_width(ots* state, otv* args, int argc) {
  RenderTexture2D* target;
  const char* who = "render-texture-width";
  if (!need_count(state, who, argc, 1) || !render_texture_payload(state, who, args[0], &target))
    return OT_UNWIND;
  return ot_make_int(target->texture.width);
}

static otv ray_render_texture_height(ots* state, otv* args, int argc) {
  RenderTexture2D* target;
  const char* who = "render-texture-height";
  if (!need_count(state, who, argc, 1) || !render_texture_payload(state, who, args[0], &target))
    return OT_UNWIND;
  return ot_make_int(target->texture.height);
}

static otv ray_draw_render_texture(ots* state, otv* args, int argc) {
  RenderTexture2D* target;
  const char* who = "draw-render-texture";
  if (!need_count(state, who, argc, 4) || !need_number(state, who, args[1]) ||
      !need_number(state, who, args[2]) || !need_int(state, who, args[3]) ||
      !need_window(state, who) || !render_texture_payload(state, who, args[0], &target))
    return OT_UNWIND;
  Rectangle source = {0.0f, 0.0f, (float)target->texture.width, (float)-target->texture.height};
  DrawTextureRec(target->texture, source,
                 (Vector2){(float)number_value(args[1]), (float)number_value(args[2])},
                 color_value(args[3]));
  return ot_nil;
}

static otv ray_draw_render_texture_pro(ots* state, otv* args, int argc) {
  RenderTexture2D* target;
  const char* who = "draw-render-texture-pro";
  if (!need_count(state, who, argc, 6)) return OT_UNWIND;
  for (int i = 1; i < 5; i++)
    if (!need_number(state, who, args[i])) return OT_UNWIND;
  if (!need_int(state, who, args[5]) || !need_window(state, who) ||
      !render_texture_payload(state, who, args[0], &target))
    return OT_UNWIND;
  Rectangle source = {0.0f, 0.0f, (float)target->texture.width, (float)-target->texture.height};
  Rectangle destination = {(float)number_value(args[1]), (float)number_value(args[2]),
                           (float)number_value(args[3]), (float)number_value(args[4])};
  DrawTexturePro(target->texture, source, destination, (Vector2){0.0f, 0.0f}, 0.0f,
                 color_value(args[5]));
  return ot_nil;
}

static otv ray_set_font_filter(ots* state, otv* args, int argc) {
  Font* font;
  const char* who = "set-font-filter!";
  if (!need_count(state, who, argc, 2) || !need_int(state, who, args[1]) ||
      !need_window(state, who) || !font_payload(state, who, args[0], &font))
    return OT_UNWIND;
  SetTextureFilter(font->texture, (int)ot_get_int(args[1]));
  return ot_nil;
}

static otv ray_set_render_filter(ots* state, otv* args, int argc) {
  RenderTexture2D* target;
  const char* who = "set-render-texture-filter!";
  if (!need_count(state, who, argc, 2) || !need_int(state, who, args[1]) ||
      !need_window(state, who) || !render_texture_payload(state, who, args[0], &target))
    return OT_UNWIND;
  SetTextureFilter(target->texture, (int)ot_get_int(args[1]));
  return ot_nil;
}

static otv ray_screenshot(ots* state, otv* args, int argc) {
  const char* who = "take-screenshot!";
  if (!need_count(state, who, argc, 1) || !need_window(state, who)) return OT_UNWIND;
  char* path = copy_cstr(state, who, args[0]);
  if (path == NULL) return OT_UNWIND;
  rlDrawRenderBatchActive();
  Image image = LoadImageFromScreen();
  bool exported = ExportImage(image, path);
  UnloadImage(image);
  ot_host_free(path);
  if (!exported) return ot_raise(state, "%s: Raylib could not export the image", who);
  return ot_nil;
}

static otv ray_file_exists(ots* state, otv* args, int argc) {
  const char* who = "file-exists?";
  if (!need_count(state, who, argc, 1)) return OT_UNWIND;
  char* path = copy_cstr(state, who, args[0]);
  if (path == NULL) return OT_UNWIND;
  bool exists = FileExists(path);
  ot_host_free(path);
  return exists ? ot_true : ot_false;
}

static otv ray_env(ots* state, otv* args, int argc) {
  const char* who = "env";
  if (!need_count(state, who, argc, 1)) return OT_UNWIND;
  char* name = copy_cstr(state, who, args[0]);
  if (name == NULL) return OT_UNWIND;
  const char* value = getenv(name);
  ot_host_free(name);
  return value == NULL ? ot_nil : ot_make_string(state, value, strlen(value));
}

typedef struct ray_nat {
  const char* name;
  ot_nat function;
} ray_nat;

static void init_ray(ots* state) {
  static const ray_nat nats[] = {
      {"init-window", ray_init_window},
      {"close-window", ray_close_window},
      {"window-ready?", ray_window_ready},
      {"window-should-close?", ray_window_should_close},
      {"set-config-flags!", ray_set_config_flags},
      {"window-resized?", ray_window_resized},
      {"window-fullscreen?", ray_window_fullscreen},
      {"toggle-fullscreen!", ray_toggle_fullscreen},
      {"set-window-size!", ray_set_window_size},
      {"set-window-title!", ray_set_window_title},
      {"set-target-fps!", ray_set_target_fps},
      {"frame-time", ray_frame_time},
      {"time", ray_time},
      {"fps", ray_fps},
      {"set-random-seed!", ray_set_seed},
      {"random-value", ray_random},
      {"screen-width", ray_screen_width},
      {"screen-height", ray_screen_height},
      {"begin-drawing", ray_begin_drawing},
      {"end-drawing", ray_end_drawing},
      {"clear-background", ray_clear_background},
      {"draw-text", ray_draw_text},
      {"draw-rectangle", ray_draw_rectangle},
      {"draw-circle", ray_draw_circle},
      {"draw-line", ray_draw_line},
      {"draw-rectangle-lines", ray_draw_rectangle_lines},
      {"begin-scissor", ray_begin_scissor},
      {"end-scissor", ray_end_scissor},
      {"key-pressed?", ray_key_pressed},
      {"key-down?", ray_key_down},
      {"key-released?", ray_key_released},
      {"next-key-pressed", ray_next_key},
      {"next-char-pressed", ray_next_char},
      {"mouse-x", ray_mouse_x},
      {"mouse-y", ray_mouse_y},
      {"mouse-button-pressed?", ray_mouse_pressed},
      {"mouse-button-down?", ray_mouse_down},
      {"mouse-button-released?", ray_mouse_released},
      {"mouse-wheel", ray_mouse_wheel},
      {"load-texture", ray_load_texture},
      {"unload-texture!", ray_unload_texture},
      {"texture-width", ray_texture_width},
      {"texture-height", ray_texture_height},
      {"draw-texture", ray_draw_texture},
      {"draw-texture-pro", ray_draw_texture_pro},
      {"default-font", ray_default_font},
      {"load-font", ray_load_font},
      {"load-font-ex", ray_load_font_ex},
      {"unload-font!", ray_unload_font},
      {"font-base-size", ray_font_base_size},
      {"draw-font", ray_draw_font},
      {"draw-codepoint", ray_draw_codepoint},
      {"measure-font-width", ray_measure_font_width},
      {"measure-font-height", ray_measure_font_height},
      {"load-render-texture", ray_load_render_texture},
      {"unload-render-texture!", ray_unload_render_texture},
      {"begin-texture-mode", ray_begin_texture_mode},
      {"end-texture-mode", ray_end_texture_mode},
      {"render-texture-width", ray_render_texture_width},
      {"render-texture-height", ray_render_texture_height},
      {"draw-render-texture", ray_draw_render_texture},
      {"draw-render-texture-pro", ray_draw_render_texture_pro},
      {"set-font-filter!", ray_set_font_filter},
      {"set-render-texture-filter!", ray_set_render_filter},
      {"take-screenshot!", ray_screenshot},
      {"file-exists?", ray_file_exists},
      {"env", ray_env},
  };
  (void)texture_type(state);
  (void)font_type(state);
  (void)render_texture_type(state);
  for (size_t i = 0; i < sizeof nats / sizeof nats[0]; i++)
    ot_def_nat(state, nats[i].name, nats[i].function);
}

void ot_register_ray_extension(ots* state) { ot_register_module(state, "ray", init_ray); }
