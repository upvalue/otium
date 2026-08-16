// Thin, allocation-conscious Raylib bindings. Vector2, Rectangle, and Color
// cross the boundary as flat numbers; only owning GPU handles use Foreign.
#include "raylib_ext.h"
#include "builtins.h"
#include "heap.h"
#include "vm.h"
#include <raylib.h>

static Value need_number(State* vm, const char* who, Value value) {
  if (value.tag != Tag_Int && value.tag != Tag_Float)
    return raise_error(vm, "%s: expected number", who);
  return nil_v();
}

static f64 number_value(Value value) { return value.tag == Tag_Float ? value.f : (f64)value.i; }

static Value need_window(State* vm, const char* who) {
  if (!IsWindowReady()) return raise_error(vm, "%s: window is not initialized", who);
  return nil_v();
}

static Value need_color(State* vm, const char* who, Value value) {
  return need_tag(vm, who, value, Tag_Int, "packed RGBA int");
}

static Color color_value(Value value) {
  u32 rgba = (u32)value.i;
  return (Color){(u8)(rgba >> 24), (u8)(rgba >> 16), (u8)(rgba >> 8), (u8)rgba};
}

static const char* string_value(Value value) { return string_data_bytes(as_string(value)); }

typedef struct FontHandle {
  Font font;
  bool owned;
} FontHandle;

static void finalize_texture(State* vm, void* payload) {
  (void)vm;
  Texture2D texture = *(Texture2D*)payload;
  if (IsWindowReady() && texture.id != 0) UnloadTexture(texture);
}

static void finalize_font(State* vm, void* payload) {
  (void)vm;
  FontHandle handle = *(FontHandle*)payload;
  if (handle.owned && IsWindowReady() && handle.font.texture.id != 0) UnloadFont(handle.font);
}

static void finalize_render_texture(State* vm, void* payload) {
  (void)vm;
  RenderTexture2D target = *(RenderTexture2D*)payload;
  if (IsWindowReady() && target.id != 0) UnloadRenderTexture(target);
}

static u32 texture_type(State* vm) {
  return register_foreign_type(vm, "raylib/texture", finalize_texture);
}

static u32 font_type(State* vm) { return register_foreign_type(vm, "raylib/font", finalize_font); }

static u32 render_texture_type(State* vm) {
  return register_foreign_type(vm, "raylib/render-texture", finalize_render_texture);
}

static Value texture_payload(State* vm, const char* who, Value value, Texture2D** out) {
  void* payload = nullptr;
  OT_TRY(foreign_check(vm, who, value, texture_type(vm), &payload));
  *out = (Texture2D*)payload;
  return nil_v();
}

static Value font_payload(State* vm, const char* who, Value value, Font** out) {
  void* payload = nullptr;
  OT_TRY(foreign_check(vm, who, value, font_type(vm), &payload));
  *out = &((FontHandle*)payload)->font;
  return nil_v();
}

static Value make_font(State* vm, Font font, bool owned) {
  FontHandle handle = {font, owned};
  return make_foreign_inline(vm, font_type(vm), &handle, sizeof handle);
}

static bool default_font(Font font) {
  Font builtin = GetFontDefault();
  return font.texture.id == builtin.texture.id && font.recs == builtin.recs &&
         font.glyphs == builtin.glyphs;
}

static Value render_texture_payload(State* vm, const char* who, Value value,
                                    RenderTexture2D** out) {
  void* payload = nullptr;
  OT_TRY(foreign_check(vm, who, value, render_texture_type(vm), &payload));
  *out = (RenderTexture2D*)payload;
  return nil_v();
}

static Value nat_init_window(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "init-window", argc, 3, 3));
  OT_TRY(need_int(vm, "init-window", ARG(0)));
  OT_TRY(need_int(vm, "init-window", ARG(1)));
  OT_TRY(need_string(vm, "init-window", ARG(2)));
  if (IsWindowReady()) return raise_error(vm, "init-window: window is already initialized");
  InitWindow((int)ARG(0).i, (int)ARG(1).i, string_value(ARG(2)));
  if (!IsWindowReady()) return raise_error(vm, "init-window: Raylib could not create the window");
  return nil_v();
}

static Value nat_close_window(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "close-window", argc, 0, 0));
  if (IsWindowReady()) CloseWindow();
  return nil_v();
}

static Value nat_window_ready(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "window-ready?", argc, 0, 0));
  return bool_v(IsWindowReady());
}

static Value nat_window_should_close(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "window-should-close?", argc, 0, 0));
  OT_TRY(need_window(vm, "window-should-close?"));
  return bool_v(WindowShouldClose());
}

static Value nat_set_config_flags(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "set-config-flags!", argc, 1, 1));
  OT_TRY(need_int(vm, "set-config-flags!", ARG(0)));
  if (IsWindowReady()) return raise_error(vm, "set-config-flags!: call this before init-window");
  SetConfigFlags((unsigned int)ARG(0).i);
  return nil_v();
}

static Value nat_window_resized(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "window-resized?", argc, 0, 0));
  OT_TRY(need_window(vm, "window-resized?"));
  return bool_v(IsWindowResized());
}

static Value nat_window_fullscreen(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "window-fullscreen?", argc, 0, 0));
  OT_TRY(need_window(vm, "window-fullscreen?"));
  return bool_v(IsWindowFullscreen());
}

static Value nat_toggle_fullscreen(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "toggle-fullscreen!", argc, 0, 0));
  OT_TRY(need_window(vm, "toggle-fullscreen!"));
  ToggleFullscreen();
  return nil_v();
}

static Value nat_set_window_size(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "set-window-size!", argc, 2, 2));
  OT_TRY(need_int(vm, "set-window-size!", ARG(0)));
  OT_TRY(need_int(vm, "set-window-size!", ARG(1)));
  OT_TRY(need_window(vm, "set-window-size!"));
  SetWindowSize((int)ARG(0).i, (int)ARG(1).i);
  return nil_v();
}

static Value nat_set_window_title(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "set-window-title!", argc, 1, 1));
  OT_TRY(need_string(vm, "set-window-title!", ARG(0)));
  OT_TRY(need_window(vm, "set-window-title!"));
  SetWindowTitle(string_value(ARG(0)));
  return nil_v();
}

static Value nat_set_target_fps(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "set-target-fps!", argc, 1, 1));
  OT_TRY(need_int(vm, "set-target-fps!", ARG(0)));
  SetTargetFPS((int)ARG(0).i);
  return nil_v();
}

static Value nat_frame_time(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "frame-time", argc, 0, 0));
  return float_v(GetFrameTime());
}

static Value nat_time(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "time", argc, 0, 0));
  OT_TRY(need_window(vm, "time"));
  return float_v(GetTime());
}

static Value nat_fps(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "fps", argc, 0, 0));
  OT_TRY(need_window(vm, "fps"));
  return int_v(GetFPS());
}

static Value nat_set_random_seed(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "set-random-seed!", argc, 1, 1));
  OT_TRY(need_int(vm, "set-random-seed!", ARG(0)));
  SetRandomSeed((unsigned int)ARG(0).i);
  return nil_v();
}

static Value nat_random_value(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "random-value", argc, 2, 2));
  OT_TRY(need_int(vm, "random-value", ARG(0)));
  OT_TRY(need_int(vm, "random-value", ARG(1)));
  if (ARG(0).i > ARG(1).i) return raise_error(vm, "random-value: minimum exceeds maximum");
  return int_v(GetRandomValue((int)ARG(0).i, (int)ARG(1).i));
}

static Value nat_screen_width(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "screen-width", argc, 0, 0));
  OT_TRY(need_window(vm, "screen-width"));
  return int_v(GetScreenWidth());
}

static Value nat_screen_height(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "screen-height", argc, 0, 0));
  OT_TRY(need_window(vm, "screen-height"));
  return int_v(GetScreenHeight());
}

static Value nat_begin_drawing(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "begin-drawing", argc, 0, 0));
  OT_TRY(need_window(vm, "begin-drawing"));
  BeginDrawing();
  return nil_v();
}

static Value nat_end_drawing(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "end-drawing", argc, 0, 0));
  OT_TRY(need_window(vm, "end-drawing"));
  EndDrawing();
  return nil_v();
}

static Value nat_clear_background(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "clear-background", argc, 1, 1));
  OT_TRY(need_color(vm, "clear-background", ARG(0)));
  OT_TRY(need_window(vm, "clear-background"));
  ClearBackground(color_value(ARG(0)));
  return nil_v();
}

static Value nat_draw_text(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "draw-text", argc, 5, 5));
  OT_TRY(need_string(vm, "draw-text", ARG(0)));
  for (u32 i = 1; i < 4; i++) OT_TRY(need_int(vm, "draw-text", ARG(i)));
  OT_TRY(need_color(vm, "draw-text", ARG(4)));
  OT_TRY(need_window(vm, "draw-text"));
  DrawText(string_value(ARG(0)), (int)ARG(1).i, (int)ARG(2).i, (int)ARG(3).i, color_value(ARG(4)));
  return nil_v();
}

static Value nat_draw_rectangle(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "draw-rectangle", argc, 5, 5));
  for (u32 i = 0; i < 4; i++) OT_TRY(need_int(vm, "draw-rectangle", ARG(i)));
  OT_TRY(need_color(vm, "draw-rectangle", ARG(4)));
  OT_TRY(need_window(vm, "draw-rectangle"));
  DrawRectangle((int)ARG(0).i, (int)ARG(1).i, (int)ARG(2).i, (int)ARG(3).i, color_value(ARG(4)));
  return nil_v();
}

static Value nat_draw_circle(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "draw-circle", argc, 4, 4));
  OT_TRY(need_int(vm, "draw-circle", ARG(0)));
  OT_TRY(need_int(vm, "draw-circle", ARG(1)));
  OT_TRY(need_number(vm, "draw-circle", ARG(2)));
  OT_TRY(need_color(vm, "draw-circle", ARG(3)));
  OT_TRY(need_window(vm, "draw-circle"));
  DrawCircle((int)ARG(0).i, (int)ARG(1).i, (float)number_value(ARG(2)), color_value(ARG(3)));
  return nil_v();
}

static Value nat_draw_line(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "draw-line", argc, 6, 6));
  for (u32 i = 0; i < 5; i++) OT_TRY(need_number(vm, "draw-line", ARG(i)));
  OT_TRY(need_color(vm, "draw-line", ARG(5)));
  OT_TRY(need_window(vm, "draw-line"));
  Vector2 start = {(float)number_value(ARG(0)), (float)number_value(ARG(1))};
  Vector2 end = {(float)number_value(ARG(2)), (float)number_value(ARG(3))};
  DrawLineEx(start, end, (float)number_value(ARG(4)), color_value(ARG(5)));
  return nil_v();
}

static Value nat_draw_rectangle_lines(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "draw-rectangle-lines", argc, 6, 6));
  for (u32 i = 0; i < 5; i++) OT_TRY(need_number(vm, "draw-rectangle-lines", ARG(i)));
  OT_TRY(need_color(vm, "draw-rectangle-lines", ARG(5)));
  OT_TRY(need_window(vm, "draw-rectangle-lines"));
  Rectangle rectangle = {(float)number_value(ARG(0)), (float)number_value(ARG(1)),
                         (float)number_value(ARG(2)), (float)number_value(ARG(3))};
  DrawRectangleLinesEx(rectangle, (float)number_value(ARG(4)), color_value(ARG(5)));
  return nil_v();
}

static Value nat_begin_scissor(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "begin-scissor", argc, 4, 4));
  for (u32 i = 0; i < 4; i++) OT_TRY(need_int(vm, "begin-scissor", ARG(i)));
  OT_TRY(need_window(vm, "begin-scissor"));
  BeginScissorMode((int)ARG(0).i, (int)ARG(1).i, (int)ARG(2).i, (int)ARG(3).i);
  return nil_v();
}

static Value nat_end_scissor(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "end-scissor", argc, 0, 0));
  OT_TRY(need_window(vm, "end-scissor"));
  EndScissorMode();
  return nil_v();
}

static Value nat_key_pressed(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "key-pressed?", argc, 1, 1));
  OT_TRY(need_int(vm, "key-pressed?", ARG(0)));
  OT_TRY(need_window(vm, "key-pressed?"));
  return bool_v(IsKeyPressed((int)ARG(0).i));
}

static Value nat_key_down(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "key-down?", argc, 1, 1));
  OT_TRY(need_int(vm, "key-down?", ARG(0)));
  OT_TRY(need_window(vm, "key-down?"));
  return bool_v(IsKeyDown((int)ARG(0).i));
}

static Value nat_key_released(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "key-released?", argc, 1, 1));
  OT_TRY(need_int(vm, "key-released?", ARG(0)));
  OT_TRY(need_window(vm, "key-released?"));
  return bool_v(IsKeyReleased((int)ARG(0).i));
}

static Value nat_next_key_pressed(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "next-key-pressed", argc, 0, 0));
  OT_TRY(need_window(vm, "next-key-pressed"));
  return int_v(GetKeyPressed());
}

static Value nat_next_char_pressed(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "next-char-pressed", argc, 0, 0));
  OT_TRY(need_window(vm, "next-char-pressed"));
  return int_v(GetCharPressed());
}

static Value nat_mouse_x(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "mouse-x", argc, 0, 0));
  OT_TRY(need_window(vm, "mouse-x"));
  return int_v(GetMouseX());
}

static Value nat_mouse_y(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "mouse-y", argc, 0, 0));
  OT_TRY(need_window(vm, "mouse-y"));
  return int_v(GetMouseY());
}

static Value nat_mouse_button_pressed(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "mouse-button-pressed?", argc, 1, 1));
  OT_TRY(need_int(vm, "mouse-button-pressed?", ARG(0)));
  OT_TRY(need_window(vm, "mouse-button-pressed?"));
  return bool_v(IsMouseButtonPressed((int)ARG(0).i));
}

static Value nat_mouse_button_down(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "mouse-button-down?", argc, 1, 1));
  OT_TRY(need_int(vm, "mouse-button-down?", ARG(0)));
  OT_TRY(need_window(vm, "mouse-button-down?"));
  return bool_v(IsMouseButtonDown((int)ARG(0).i));
}

static Value nat_mouse_button_released(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "mouse-button-released?", argc, 1, 1));
  OT_TRY(need_int(vm, "mouse-button-released?", ARG(0)));
  OT_TRY(need_window(vm, "mouse-button-released?"));
  return bool_v(IsMouseButtonReleased((int)ARG(0).i));
}

static Value nat_mouse_wheel(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "mouse-wheel", argc, 0, 0));
  OT_TRY(need_window(vm, "mouse-wheel"));
  return float_v(GetMouseWheelMove());
}

static Value nat_load_texture(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "load-texture", argc, 1, 1));
  OT_TRY(need_string(vm, "load-texture", ARG(0)));
  OT_TRY(need_window(vm, "load-texture"));
  Texture2D texture = LoadTexture(string_value(ARG(0)));
  if (texture.id == 0) return raise_error(vm, "load-texture: Raylib could not load the texture");
  return make_foreign_inline(vm, texture_type(vm), &texture, sizeof texture);
}

static Value nat_unload_texture(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "unload-texture!", argc, 1, 1));
  return foreign_release(vm, "unload-texture!", ARG(0), texture_type(vm));
}

static Value nat_texture_width(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "texture-width", argc, 1, 1));
  Texture2D* texture = nullptr;
  OT_TRY(texture_payload(vm, "texture-width", ARG(0), &texture));
  return int_v(texture->width);
}

static Value nat_texture_height(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "texture-height", argc, 1, 1));
  Texture2D* texture = nullptr;
  OT_TRY(texture_payload(vm, "texture-height", ARG(0), &texture));
  return int_v(texture->height);
}

static Value nat_draw_texture(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "draw-texture", argc, 4, 4));
  OT_TRY(need_int(vm, "draw-texture", ARG(1)));
  OT_TRY(need_int(vm, "draw-texture", ARG(2)));
  OT_TRY(need_color(vm, "draw-texture", ARG(3)));
  OT_TRY(need_window(vm, "draw-texture"));
  Texture2D* texture = nullptr;
  OT_TRY(texture_payload(vm, "draw-texture", ARG(0), &texture));
  DrawTexture(*texture, (int)ARG(1).i, (int)ARG(2).i, color_value(ARG(3)));
  return nil_v();
}

static Value nat_draw_texture_pro(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "draw-texture-pro", argc, 13, 13));
  for (u32 i = 1; i < 12; i++) OT_TRY(need_number(vm, "draw-texture-pro", ARG(i)));
  OT_TRY(need_color(vm, "draw-texture-pro", ARG(12)));
  OT_TRY(need_window(vm, "draw-texture-pro"));
  Texture2D* texture = nullptr;
  OT_TRY(texture_payload(vm, "draw-texture-pro", ARG(0), &texture));
  Rectangle source = {(float)number_value(ARG(1)), (float)number_value(ARG(2)),
                      (float)number_value(ARG(3)), (float)number_value(ARG(4))};
  Rectangle destination = {(float)number_value(ARG(5)), (float)number_value(ARG(6)),
                           (float)number_value(ARG(7)), (float)number_value(ARG(8))};
  Vector2 origin = {(float)number_value(ARG(9)), (float)number_value(ARG(10))};
  DrawTexturePro(*texture, source, destination, origin, (float)number_value(ARG(11)),
                 color_value(ARG(12)));
  return nil_v();
}

static Value nat_default_font(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "default-font", argc, 0, 0));
  OT_TRY(need_window(vm, "default-font"));
  return make_font(vm, GetFontDefault(), false);
}

static Value nat_load_font(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "load-font", argc, 1, 1));
  OT_TRY(need_string(vm, "load-font", ARG(0)));
  OT_TRY(need_window(vm, "load-font"));
  Font font = LoadFont(string_value(ARG(0)));
  if (font.texture.id == 0 || font.glyphCount == 0 || default_font(font))
    return raise_error(vm, "load-font: Raylib could not load the font");
  return make_font(vm, font, true);
}

static Value nat_load_font_ex(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "load-font-ex", argc, 2, 2));
  OT_TRY(need_string(vm, "load-font-ex", ARG(0)));
  OT_TRY(need_int(vm, "load-font-ex", ARG(1)));
  OT_TRY(need_window(vm, "load-font-ex"));
  Font font = LoadFontEx(string_value(ARG(0)), (int)ARG(1).i, nullptr, 0);
  if (font.texture.id == 0 || font.glyphCount == 0 || default_font(font))
    return raise_error(vm, "load-font-ex: Raylib could not load the font");
  return make_font(vm, font, true);
}

static Value nat_unload_font(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "unload-font!", argc, 1, 1));
  return foreign_release(vm, "unload-font!", ARG(0), font_type(vm));
}

static Value nat_font_base_size(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "font-base-size", argc, 1, 1));
  Font* font = nullptr;
  OT_TRY(font_payload(vm, "font-base-size", ARG(0), &font));
  return int_v(font->baseSize);
}

static Value nat_draw_font(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "draw-font", argc, 7, 7));
  OT_TRY(need_string(vm, "draw-font", ARG(1)));
  for (u32 i = 2; i < 6; i++) OT_TRY(need_number(vm, "draw-font", ARG(i)));
  OT_TRY(need_color(vm, "draw-font", ARG(6)));
  OT_TRY(need_window(vm, "draw-font"));
  Font* font = nullptr;
  OT_TRY(font_payload(vm, "draw-font", ARG(0), &font));
  DrawTextEx(*font, string_value(ARG(1)),
             (Vector2){(float)number_value(ARG(2)), (float)number_value(ARG(3))},
             (float)number_value(ARG(4)), (float)number_value(ARG(5)), color_value(ARG(6)));
  return nil_v();
}

static Value nat_draw_codepoint(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "draw-codepoint", argc, 6, 6));
  OT_TRY(need_int(vm, "draw-codepoint", ARG(1)));
  for (u32 i = 2; i < 5; i++) OT_TRY(need_number(vm, "draw-codepoint", ARG(i)));
  OT_TRY(need_color(vm, "draw-codepoint", ARG(5)));
  OT_TRY(need_window(vm, "draw-codepoint"));
  Font* font = nullptr;
  OT_TRY(font_payload(vm, "draw-codepoint", ARG(0), &font));
  Vector2 position = {(float)number_value(ARG(2)), (float)number_value(ARG(3))};
  DrawTextCodepoint(*font, (int)ARG(1).i, position, (float)number_value(ARG(4)),
                    color_value(ARG(5)));
  return nil_v();
}

static Value measure_font(State* vm, u32 base, u32 argc, bool width) {
  const char* who = width ? "measure-font-width" : "measure-font-height";
  OT_TRY(need_argc(vm, who, argc, 4, 4));
  OT_TRY(need_string(vm, who, ARG(1)));
  OT_TRY(need_number(vm, who, ARG(2)));
  OT_TRY(need_number(vm, who, ARG(3)));
  OT_TRY(need_window(vm, who));
  Font* font = nullptr;
  OT_TRY(font_payload(vm, who, ARG(0), &font));
  Vector2 size = MeasureTextEx(*font, string_value(ARG(1)), (float)number_value(ARG(2)),
                               (float)number_value(ARG(3)));
  return float_v(width ? size.x : size.y);
}

static Value nat_measure_font_width(State* vm, u32 base, u32 argc) {
  return measure_font(vm, base, argc, true);
}

static Value nat_measure_font_height(State* vm, u32 base, u32 argc) {
  return measure_font(vm, base, argc, false);
}

static Value nat_load_render_texture(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "load-render-texture", argc, 2, 2));
  OT_TRY(need_int(vm, "load-render-texture", ARG(0)));
  OT_TRY(need_int(vm, "load-render-texture", ARG(1)));
  OT_TRY(need_window(vm, "load-render-texture"));
  RenderTexture2D target = LoadRenderTexture((int)ARG(0).i, (int)ARG(1).i);
  if (target.id == 0)
    return raise_error(vm, "load-render-texture: Raylib could not create the target");
  return make_foreign_inline(vm, render_texture_type(vm), &target, sizeof target);
}

static Value nat_unload_render_texture(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "unload-render-texture!", argc, 1, 1));
  return foreign_release(vm, "unload-render-texture!", ARG(0), render_texture_type(vm));
}

static Value nat_begin_texture_mode(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "begin-texture-mode", argc, 1, 1));
  OT_TRY(need_window(vm, "begin-texture-mode"));
  RenderTexture2D* target = nullptr;
  OT_TRY(render_texture_payload(vm, "begin-texture-mode", ARG(0), &target));
  BeginTextureMode(*target);
  return nil_v();
}

static Value nat_end_texture_mode(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "end-texture-mode", argc, 0, 0));
  OT_TRY(need_window(vm, "end-texture-mode"));
  EndTextureMode();
  return nil_v();
}

static Value nat_render_texture_width(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "render-texture-width", argc, 1, 1));
  RenderTexture2D* target = nullptr;
  OT_TRY(render_texture_payload(vm, "render-texture-width", ARG(0), &target));
  return int_v(target->texture.width);
}

static Value nat_render_texture_height(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "render-texture-height", argc, 1, 1));
  RenderTexture2D* target = nullptr;
  OT_TRY(render_texture_payload(vm, "render-texture-height", ARG(0), &target));
  return int_v(target->texture.height);
}

static Value nat_draw_render_texture(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "draw-render-texture", argc, 4, 4));
  OT_TRY(need_number(vm, "draw-render-texture", ARG(1)));
  OT_TRY(need_number(vm, "draw-render-texture", ARG(2)));
  OT_TRY(need_color(vm, "draw-render-texture", ARG(3)));
  OT_TRY(need_window(vm, "draw-render-texture"));
  RenderTexture2D* target = nullptr;
  OT_TRY(render_texture_payload(vm, "draw-render-texture", ARG(0), &target));
  Rectangle source = {0.0f, 0.0f, (float)target->texture.width, (float)-target->texture.height};
  Vector2 position = {(float)number_value(ARG(1)), (float)number_value(ARG(2))};
  DrawTextureRec(target->texture, source, position, color_value(ARG(3)));
  return nil_v();
}

static Value nat_draw_render_texture_pro(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "draw-render-texture-pro", argc, 6, 6));
  for (u32 i = 1; i < 5; i++) OT_TRY(need_number(vm, "draw-render-texture-pro", ARG(i)));
  OT_TRY(need_color(vm, "draw-render-texture-pro", ARG(5)));
  OT_TRY(need_window(vm, "draw-render-texture-pro"));
  RenderTexture2D* target = nullptr;
  OT_TRY(render_texture_payload(vm, "draw-render-texture-pro", ARG(0), &target));
  Rectangle source = {0.0f, 0.0f, (float)target->texture.width, (float)-target->texture.height};
  Rectangle destination = {(float)number_value(ARG(1)), (float)number_value(ARG(2)),
                           (float)number_value(ARG(3)), (float)number_value(ARG(4))};
  DrawTexturePro(target->texture, source, destination, (Vector2){0.0f, 0.0f}, 0.0f,
                 color_value(ARG(5)));
  return nil_v();
}

static void init_raylib(State* vm) {
  (void)texture_type(vm);
  (void)font_type(vm);
  (void)render_texture_type(vm);
  def_native(vm, "init-window", nat_init_window);
  def_native(vm, "close-window", nat_close_window);
  def_native(vm, "window-ready?", nat_window_ready);
  def_native(vm, "window-should-close?", nat_window_should_close);
  def_native(vm, "set-config-flags!", nat_set_config_flags);
  def_native(vm, "window-resized?", nat_window_resized);
  def_native(vm, "window-fullscreen?", nat_window_fullscreen);
  def_native(vm, "toggle-fullscreen!", nat_toggle_fullscreen);
  def_native(vm, "set-window-size!", nat_set_window_size);
  def_native(vm, "set-window-title!", nat_set_window_title);
  def_native(vm, "set-target-fps!", nat_set_target_fps);
  def_native(vm, "frame-time", nat_frame_time);
  def_native(vm, "time", nat_time);
  def_native(vm, "fps", nat_fps);
  def_native(vm, "set-random-seed!", nat_set_random_seed);
  def_native(vm, "random-value", nat_random_value);
  def_native(vm, "screen-width", nat_screen_width);
  def_native(vm, "screen-height", nat_screen_height);
  def_native(vm, "begin-drawing", nat_begin_drawing);
  def_native(vm, "end-drawing", nat_end_drawing);
  def_native(vm, "clear-background", nat_clear_background);
  def_native(vm, "draw-text", nat_draw_text);
  def_native(vm, "draw-rectangle", nat_draw_rectangle);
  def_native(vm, "draw-circle", nat_draw_circle);
  def_native(vm, "draw-line", nat_draw_line);
  def_native(vm, "draw-rectangle-lines", nat_draw_rectangle_lines);
  def_native(vm, "begin-scissor", nat_begin_scissor);
  def_native(vm, "end-scissor", nat_end_scissor);
  def_native(vm, "key-pressed?", nat_key_pressed);
  def_native(vm, "key-down?", nat_key_down);
  def_native(vm, "key-released?", nat_key_released);
  def_native(vm, "next-key-pressed", nat_next_key_pressed);
  def_native(vm, "next-char-pressed", nat_next_char_pressed);
  def_native(vm, "mouse-x", nat_mouse_x);
  def_native(vm, "mouse-y", nat_mouse_y);
  def_native(vm, "mouse-button-pressed?", nat_mouse_button_pressed);
  def_native(vm, "mouse-button-down?", nat_mouse_button_down);
  def_native(vm, "mouse-button-released?", nat_mouse_button_released);
  def_native(vm, "mouse-wheel", nat_mouse_wheel);
  def_native(vm, "load-texture", nat_load_texture);
  def_native(vm, "unload-texture!", nat_unload_texture);
  def_native(vm, "texture-width", nat_texture_width);
  def_native(vm, "texture-height", nat_texture_height);
  def_native(vm, "draw-texture", nat_draw_texture);
  def_native(vm, "draw-texture-pro", nat_draw_texture_pro);
  def_native(vm, "default-font", nat_default_font);
  def_native(vm, "load-font", nat_load_font);
  def_native(vm, "load-font-ex", nat_load_font_ex);
  def_native(vm, "unload-font!", nat_unload_font);
  def_native(vm, "font-base-size", nat_font_base_size);
  def_native(vm, "draw-font", nat_draw_font);
  def_native(vm, "draw-codepoint", nat_draw_codepoint);
  def_native(vm, "measure-font-width", nat_measure_font_width);
  def_native(vm, "measure-font-height", nat_measure_font_height);
  def_native(vm, "load-render-texture", nat_load_render_texture);
  def_native(vm, "unload-render-texture!", nat_unload_render_texture);
  def_native(vm, "begin-texture-mode", nat_begin_texture_mode);
  def_native(vm, "end-texture-mode", nat_end_texture_mode);
  def_native(vm, "render-texture-width", nat_render_texture_width);
  def_native(vm, "render-texture-height", nat_render_texture_height);
  def_native(vm, "draw-render-texture", nat_draw_render_texture);
  def_native(vm, "draw-render-texture-pro", nat_draw_render_texture_pro);
}

void register_raylib_extension(State* vm) { register_native_module(vm, "raylib", init_raylib); }
