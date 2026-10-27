/* otos.gfx: drawing on the virtio-gpu display from Otium. */
#include "otium.h"
#include "display.h"
#include "kernel.h"

#include <math.h>

static bool int_args(const ot_value* args, size_t count, int* out) {
  for (size_t i = 0; i < count; i++) {
    double value;
    if (!ot_number_value(args[i], &value) || !isfinite(value) || fabs(value) > 1e9) return false;
    out[i] = (int)floor(value);
  }
  return true;
}

/* Colors are 0xRRGGBB with an optional alpha from 0 to 255, since rv32
 * integers stop at 30 bits and can't hold 0xRRGGBBAA. Returns 0xRRGGBBAA. */
static bool color_args(const ot_value* args, size_t count, uint32_t* out) {
  double rgb, alpha = 255;
  if (count < 1 || count > 2 || !ot_number_value(args[0], &rgb) || rgb < 0 || rgb > 0xffffff ||
      (count == 2 && (!ot_number_value(args[1], &alpha) || alpha < 0 || alpha > 255)))
    return false;
  *out = (uint32_t)rgb << 8 | (uint32_t)alpha;
  return true;
}

static display* need_display(ot_state* rt, const char* name) {
  display* d = display_get();
  if (d == NULL) ot_native_error(rt, "%s: no display; run with just run-display", name);
  return d;
}

static ot_value gfx_size(ot_state* rt, void* userdata, const ot_value* args, size_t count) {
  (void)userdata;
  (void)args;
  (void)count;
  display* d = need_display(rt, "size");
  if (d == NULL) return 0;
  ot_value size[] = {ot_int(d->width), ot_int(d->height)};
  return ot_make_list(rt, size, 2);
}

static ot_value gfx_fill_rect(ot_state* rt, void* userdata, const ot_value* args, size_t count) {
  (void)userdata;
  int bounds[4];
  uint32_t color;
  if (!int_args(args, 4, bounds) || !color_args(args + 4, count - 4, &color))
    return ot_native_error(rt, "fill-rect: expected x y width height color [alpha]");
  display* d = need_display(rt, "fill-rect");
  if (d == NULL) return 0;
  display_fill_rect(d, bounds[0], bounds[1], bounds[2], bounds[3], color);
  return OT_NIL;
}

static ot_value gfx_clear(ot_state* rt, void* userdata, const ot_value* args, size_t count) {
  (void)userdata;
  uint32_t color;
  if (!color_args(args, count, &color)) return ot_native_error(rt, "clear: expected a color");
  display* d = need_display(rt, "clear");
  if (d == NULL) return 0;
  display_fill_rect(d, 0, 0, d->width, d->height, color | 0xff);
  return OT_NIL;
}

static ot_value gfx_flush(ot_state* rt, void* userdata, const ot_value* args, size_t count) {
  (void)userdata;
  (void)args;
  (void)count;
  display* d = need_display(rt, "flush");
  if (d == NULL) return 0;
  display_flush(d);
  return OT_NIL;
}

bool gfx_register(ot_state* rt) {
  static const ot_native natives[] = {
      {"size", gfx_size, 0, 0, NULL},
      {"clear", gfx_clear, 1, 2, NULL},
      {"fill-rect", gfx_fill_rect, 5, 6, NULL},
      {"flush", gfx_flush, 0, 0, NULL},
  };
  return ot_define_natives(rt, "otos.gfx", natives, sizeof natives / sizeof natives[0]);
}
