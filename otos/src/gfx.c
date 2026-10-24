/* otos.gfx: drawing on the virtio-gpu display from Otium. */
#define OT_INTERNAL
#include "otium.h"
#include "display.h"
#include "kernel.h"

#include <math.h>

static bool number_arg(otv value, double* out) {
  if (ot_is_int(value)) {
    *out = (double)ot_get_int(value);
    return true;
  }
  return ot_float_value(value, out);
}

static bool int_args(otv* args, int count, int* out) {
  for (int i = 0; i < count; i++) {
    double value;
    if (!number_arg(args[i], &value) || !isfinite(value) || fabs(value) > 1e9) return false;
    out[i] = (int)floor(value);
  }
  return true;
}

/* Colors are 0xRRGGBB with an optional alpha from 0 to 255, since rv32
 * integers stop at 30 bits and can't hold 0xRRGGBBAA. Returns 0xRRGGBBAA. */
static bool color_args(otv* args, int argc, uint32_t* out) {
  double rgb, alpha = 255;
  if (argc < 1 || argc > 2 || !number_arg(args[0], &rgb) || rgb < 0 || rgb > 0xffffff ||
      (argc == 2 && (!number_arg(args[1], &alpha) || alpha < 0 || alpha > 255)))
    return false;
  *out = (uint32_t)rgb << 8 | (uint32_t)alpha;
  return true;
}

static display* need_display(ots* state, const char* name) {
  display* d = display_get();
  if (d == NULL) ot_raise(state, "%s: no display; run with just run-display", name);
  return d;
}

static otv gfx_size(ots* state, otv* args, int argc) {
  (void)args;
  if (argc != 0) return ot_raise(state, "size: expected no arguments");
  display* d = need_display(state, "size");
  if (d == NULL) return OT_UNWIND;
  otv tail = ot_cons(state, ot_make_int(d->height), ot_null);
  return ot_cons(state, ot_make_int(d->width), tail);
}

static otv gfx_fill_rect(ots* state, otv* args, int argc) {
  int bounds[4];
  uint32_t color;
  if (argc < 5 || !int_args(args, 4, bounds) || !color_args(args + 4, argc - 4, &color))
    return ot_raise(state, "fill-rect: expected x y width height color [alpha]");
  display* d = need_display(state, "fill-rect");
  if (d == NULL) return OT_UNWIND;
  display_fill_rect(d, bounds[0], bounds[1], bounds[2], bounds[3], color);
  return ot_nil;
}

static otv gfx_clear(ots* state, otv* args, int argc) {
  uint32_t color;
  if (!color_args(args, argc, &color)) return ot_raise(state, "clear: expected a color");
  display* d = need_display(state, "clear");
  if (d == NULL) return OT_UNWIND;
  display_fill_rect(d, 0, 0, d->width, d->height, color | 0xff);
  return ot_nil;
}

static otv gfx_flush(ots* state, otv* args, int argc) {
  (void)args;
  if (argc != 0) return ot_raise(state, "flush: expected no arguments");
  display* d = need_display(state, "flush");
  if (d == NULL) return OT_UNWIND;
  display_flush(d);
  return ot_nil;
}

static void gfx_module(ots* state) {
  ot_def_nat(state, "size", gfx_size);
  ot_def_nat(state, "clear", gfx_clear);
  ot_def_nat(state, "fill-rect", gfx_fill_rect);
  ot_def_nat(state, "flush", gfx_flush);
}

void gfx_register(ots* state) { ot_register_module(state, "otos.gfx", gfx_module); }
