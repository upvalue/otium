// One row of terminal cells as runs of text and spans of background, in a
// single call.
//
// Walking cells from Otium costs several FFI calls per cell: about 4.5 ms a
// frame for a 200x60 grid that scrolls, most of the terminal's paint time.
// This walks the row here and hands back what manse.terminal/paint-row draws:
// runs of cells sharing a foreground colour and style, their graphemes
// separated by 0x1F so each can be placed in its own column, and spans of
// cells sharing a background. The colour rules match paint-row-cells'.
//
// Built without libghostty-vt, manse_row_runs returns -1 and Manse walks the
// cells from Otium instead.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Style flags, as manse.ghostty defines them.
enum { BOLD = 1, ITALIC = 2, FAINT = 4, INVERSE = 8, INVISIBLE = 16, STRIKE = 32, UNDERLINE = 64 };

// Flags that change how a run is drawn, so they end it.
#define RUN_FLAGS (BOLD | ITALIC | STRIKE | UNDERLINE)

// Faint text is drawn halfway to its background.
static uint32_t faint(uint32_t fg, uint32_t bg) {
  uint32_t out = 255;
  for (int shift = 8; shift < 32; shift += 8)
    out |= ((((fg >> shift) & 255) + ((bg >> shift) & 255)) / 2) << shift;
  return out;
}

#ifdef MANSE_GHOSTTY
#include <ghostty/vt.h>

static uint32_t rgba(GhosttyColorRgb c) {
  return ((uint32_t)c.r << 24) | ((uint32_t)c.g << 16) | ((uint32_t)c.b << 8) | 255;
}

// Colours always carry alpha 255, so 0 means "none".
static uint32_t cell_color(GhosttyRenderStateRowCells cells, GhosttyRenderStateRowCellsData key) {
  GhosttyColorRgb c;
  return ghostty_render_state_row_cells_get(cells, key, &c) == GHOSTTY_SUCCESS ? rgba(c) : 0;
}

static uint32_t style_flags(const GhosttyStyle* s) {
  return (s->bold ? BOLD : 0) | (s->italic ? ITALIC : 0) | (s->faint ? FAINT : 0) |
         (s->inverse ? INVERSE : 0) | (s->invisible ? INVISIBLE : 0) |
         (s->strikethrough ? STRIKE : 0) | (s->underline ? UNDERLINE : 0);
}
#endif

// Fill text with the row's UTF-8, runs with (column, first codepoint,
// codepoint count, RGBA, style flags) and spans with (first column, end
// column, RGBA), and counts with the number of runs, spans and text bytes.
// Returns 0, -1 without libghostty-vt, -2 when a buffer is too small and -3
// when libghostty-vt fails.
int manse_row_runs(void* rows, void* row_cells, uint32_t default_fg, uint32_t default_bg,
                   uint8_t* text, int32_t text_cap, uint32_t* runs, int32_t run_cap,
                   uint32_t* spans, int32_t span_cap, int32_t* counts) {
#ifndef MANSE_GHOSTTY
  (void)rows, (void)row_cells, (void)default_fg, (void)default_bg, (void)text, (void)text_cap;
  (void)runs, (void)run_cap, (void)spans, (void)span_cap, (void)counts;
  (void)faint;
  return -1;
#else
  GhosttyRenderStateRowCells cells = row_cells;
  GhosttyResult r = ghostty_render_state_row_get(rows, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &cells);
  if (r != GHOSTTY_SUCCESS) return -3;
  int32_t len = 0, codepoints = 0, run_count = 0, span_count = 0;
  uint32_t run_fg = 0, run_flags = 0, span_bg = 0;
  for (uint32_t x = 0; ghostty_render_state_row_cells_next(cells); x++) {
    bool styled = false;
    ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_HAS_STYLING,
                                       &styled);
    // Leave a byte before the grapheme for the separator it may need.
    if (len + 1 >= text_cap) return -2;
    GhosttyBuffer buffer = {.ptr = text + len + 1, .cap = (size_t)(text_cap - len - 1)};
    r = ghostty_render_state_row_cells_get(
        cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_UTF8, &buffer);
    if (r == GHOSTTY_OUT_OF_SPACE) return -2;
    if (r != GHOSTTY_SUCCESS) return -3;
    size_t n = buffer.len;
    uint32_t cell_fg = styled ? cell_color(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR) : 0;
    // Erased cells can carry a background in the content tag without a style.
    uint32_t cell_bg =
        styled || n == 0 ? cell_color(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR) : 0;
    uint32_t flags = 0;
    if (styled) {
      GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
      if (ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE,
                                             &style) == GHOSTTY_SUCCESS)
        flags = style_flags(&style);
    }
    bool inverse = flags & INVERSE;
    uint32_t fg = inverse ? (cell_bg ? cell_bg : default_bg) : (cell_fg ? cell_fg : default_fg);
    uint32_t bg = inverse ? (cell_fg ? cell_fg : default_fg) : cell_bg;
    if (flags & FAINT) fg = faint(fg, bg ? bg : default_bg);

    if (bg && bg == span_bg) {
      spans[span_count * 3 - 2] = x + 1;
    } else if (bg) {
      if (span_count == span_cap) return -2;
      spans[span_count * 3] = x;
      spans[span_count * 3 + 1] = x + 1;
      spans[span_count * 3 + 2] = bg;
      span_count++;
    }
    span_bg = bg;
    if (!run_count || fg != run_fg || (flags & RUN_FLAGS) != run_flags) {
      if (run_count == run_cap) return -2;
      if (run_count) runs[run_count * 5 - 3] = (uint32_t)codepoints - runs[run_count * 5 - 4];
      runs[run_count * 5] = x;
      runs[run_count * 5 + 1] = (uint32_t)codepoints;
      runs[run_count * 5 + 3] = fg;
      runs[run_count * 5 + 4] = flags & RUN_FLAGS;
      run_count++;
      run_fg = fg;
      run_flags = flags & RUN_FLAGS;
      memmove(text + len, text + len + 1, n);
    } else {
      text[len++] = 0x1f;
      codepoints++;
    }
    // An empty or invisible cell is drawn as a space.
    if (n == 0 || (flags & INVISIBLE)) {
      text[len] = ' ';
      n = 1;
    }
    for (size_t i = 0; i < n; i++)
      if ((text[len + i] & 0xC0) != 0x80) codepoints++;
    len += (int32_t)n;
  }
  if (run_count) runs[run_count * 5 - 3] = (uint32_t)codepoints - runs[run_count * 5 - 4];
  counts[0] = run_count;
  counts[1] = span_count;
  counts[2] = len;
  return 0;
#endif
}
