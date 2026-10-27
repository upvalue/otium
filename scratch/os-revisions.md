# otos graphics and a platform-independent UI

Notes on getting Otium's UI framework running on otos without forking it.
Written 2026-10-23, after the first virtio-gpu driver (`otos/src/display.c`)
could fill rectangles from the REPL.

The short version: support both platforms with one UI stack. Very little of it
depends on the platform, and that part is easy to isolate.

## How the UI is layered today

| Layer | Depends on |
|---|---|
| `otium.ui` (widget tree, lifecycle) | plain Otium, but the palette uses `0xRRGGBBAA` literals |
| `otium.ui.input` (hit testing, focus, dispatch) | plain Otium; events are data |
| `otium.ui.gpu` (layout and paint passes) | Yoga (C++, `native/ui/layout.cpp`), SDL_ttf for text measurement |
| `otium.gfx` (rect, text, image, cached draws) | the native renderer |
| `otium.ui.window` (window, event loop) | SDL, plus Objective-C for Metal |
| `native/ui/renderer.c` | SDL GPU, SDL_ttf, plutosvg |

The native layers are reached through `otium.ffi`, which needs libffi and
`dlopen`. otos has neither, so `otium.ffi` is currently unavailable there.

## What actually differs between platforms

`native/ui/renderer.c` already turns everything it draws (rectangles, rounded
corners, glyphs, SVGs) into a CPU-side list of rectangle instances, each with
rect, color, shape, clip and atlas coordinates, plus a CPU-side atlas of
glyph and SVG coverage. SDL only uploads that list and draws it with one
20-line fragment shader (`native/ui/shaders/rect.frag`).

A software backend can draw the same list by doing that shader's math in C
(clip, rounded-rect distance, atlas sampling, blend). Probably around 150
lines. Only the final "draw this frame" step differs, chosen at compile time,
so it costs nothing at runtime.

That leaves three platform-specific pieces:

1. **Drawing the frame:** SDL GPU on macOS and Linux, a software rasterizer
   over the virtio-gpu framebuffer on otos.
2. **Window and events:** `otium.ui.window`. This is the only Otium file that
   needs to differ.
3. **Finding C functions:** `dlsym` on the host. On otos, libffi (it has a
   riscv32 port) plus a built-in symbol table would let `otium.ui.native` run
   unchanged.

Everything else is shared: `otium.ui`, `otium.ui.input`, `otium.ui.gpu`,
`otium.gfx`, the renderer's recording side, text, SVG and layout.

## Choosing implementations per platform

Use `cond-expand` at the top of a module:

```
(cond-expand
  (otos (require (otium.ui.otos :as host)))
  (else (require (otium.ui.sdl :as host))))
```

It's resolved when the file loads, so there's no runtime check. Calls like
`host/open` go through a var cell, which is how every global call in Otium
already works, so there's no added dispatch cost. Re-export isn't needed:
`otium.ui.window` stays as the shared part and calls whichever `host` was
loaded.

Otium doesn't have `cond-expand` yet. R7RS defines it, Chez has it natively,
and both runtimes share `lib/otium/expander.ot`, so it gets implemented once.
It needs a feature list per runtime: runtime (`c`, `chez`), OS (`macos`,
`linux`, `otos`) and architecture.

## Layout: Yoga

Adding C++ is fine. Yoga allocates in these places:

- One `new yoga::Node` per node, plus a children vector that grows as children
  are added. The UI keeps nodes across frames and creates them when widgets
  mount, so this tracks tree changes, not frames.
- A small vector per flex line per container on each layout pass
  (`algorithm/FlexLine.cpp`, `reserve` then `push_back`). This is the only
  steady-state allocation. If it matters, a reused scratch buffer is a small
  patch.
- Style values in unusual units spill into a vector (rare). The fatal-assert
  path throws.

Measurement caching uses fixed arrays (8 entries per node). Node size hasn't
been measured.

Getting it onto otos needs C++ headers (vector, array), `operator new` and
`delete` over `malloc`, and building with `-fno-exceptions`, which means
patching the one `throw` in `debug/AssertFatal.cpp`.

## Text

SDL_ttf is mostly FreeType (draws glyphs) and HarfBuzz (glyph order and
positions). Use them directly instead: same quality as now because it's the
same code, and one implementation everywhere. Both build without an OS.
FreeType takes custom memory and file functions, and HarfBuzz builds without
exceptions and with a custom allocator. Each is roughly 1MB of code, which is
fine on otos.

What we'd write ourselves, since SDL_ttf provides it now: line wrapping,
caret positions (from HarfBuzz cluster data) and font registration. Glyph
caching is already ours, in the atlas.

The alternative is kb_text_shape, a single-header C library for shaping and
line breaking that doesn't allocate, paired with stb_truetype or FreeType for
drawing. Its quality hasn't been checked here. FreeType plus HarfBuzz is the
known quantity.

## Colors and 32-bit integers

Otium values are machine words. Integers are stored directly in the word with
the low bit set to mark them, which leaves 63 bits on 64-bit machines and 31
on rv32 (about ±1 billion). `0xRRGGBBAA` needs 32 unsigned bits, so it fits on
the Mac but not on otos. The reader rejects the literal, and `+` and `*` wrap
silently past the range (`nat_add` in `c/src/otium.c`).

That makes it a language problem that colors happened to expose. The three
runtimes disagree today:

- C on 64-bit wraps at 63 bits.
- Chez has unbounded integers.
- C on rv32 wraps at 31 bits.

Nanosecond clocks, millisecond timestamps (2^30 ms is about 12 days), file
sizes and hashes would break on otos the same way.

Options:

- **Fix it in the language.** On 32-bit builds, values outside 31 bits become
  boxed 64-bit integers. Arithmetic only takes the slow path on overflow, and
  a palette color is boxed once, when it's read. Colors work unchanged and
  rv32 behaves more like the other two.
- **Change how colors are written,** e.g. `0xRRGGBB` plus a separate alpha
  (what `otos.gfx` does for now), or keyword names resolved by `ui/color`.
  Less work, but it only fixes colors.

Fixing it in the language seems better, as its own piece of work.

## Risks

The structure looks fine. Speed is the open question. Redrawing 1280×800 in
software on emulated rv32 might be too slow for full redraws. If so, redraw
only the regions that changed; the renderer's per-widget caches help there.

## Order of work

Test the risky part first:

1. Software rasterizer for the instance list, measured on otos.
2. Replace SDL_ttf with FreeType plus HarfBuzz on the Mac and confirm the
   output looks the same. Then build both for otos.
3. Yoga and C++ on otos.
4. libffi plus a built-in symbol table on otos.
5. `cond-expand`, then split `otium.ui.window` into a shared part and two
   platform parts.
6. 64-bit integers on 32-bit builds (independent, any time).

## Open decisions

- Fix integers in the language, or just change how colors are written?
- libffi on otos (one `otium.ui.native` for both), or direct natives on otos?
- Is the rasterizer speed test the right first step?
