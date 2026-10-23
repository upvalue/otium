/* A small line editor for the UART console. It redraws the whole line after
 * each change and assumes the line fits on one terminal row, with one column
 * per character. */
#include "line.h"
#include "kernel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HISTORY_MAX 100
/* Outside the byte range, so it can share a switch with typed bytes. */
#define KEY_DELETE 0x100

static char* history[HISTORY_MAX];
static int history_count;

void line_history_add(const char* line, int length) {
  if (length == 0) return;
  if (history_count > 0) {
    const char* last = history[history_count - 1];
    if ((int)strlen(last) == length && memcmp(last, line, (size_t)length) == 0) return;
  }
  char* copy = malloc((size_t)length + 1);
  if (copy == NULL) return;
  memcpy(copy, line, (size_t)length);
  copy[length] = '\0';
  if (history_count == HISTORY_MAX) {
    free(history[0]);
    memmove(history, history + 1, (HISTORY_MAX - 1) * sizeof history[0]);
    history_count--;
  }
  history[history_count++] = copy;
}

typedef struct editor {
  const char* prompt;
  char* line;
  int capacity;
  int length;
  int cursor;
} editor;

static bool continuation(char c) { return ((unsigned char)c & 0xc0) == 0x80; }

static int previous_char(const editor* e, int position) {
  if (position > 0) position--;
  while (position > 0 && continuation(e->line[position])) position--;
  return position;
}

static int next_char(const editor* e, int position) {
  if (position < e->length) position++;
  while (position < e->length && continuation(e->line[position])) position++;
  return position;
}

static void put(const char* text) { console_write(text, strlen(text)); }

static void redraw(const editor* e) {
  put("\r");
  put(e->prompt);
  console_write(e->line, (size_t)e->length);
  put("\x1b[K");
  int columns = 0;
  for (int i = e->cursor; i < e->length; i++)
    if (!continuation(e->line[i])) columns++;
  if (columns > 0) {
    char move[16];
    snprintf(move, sizeof move, "\x1b[%dD", columns);
    put(move);
  }
}

static void delete_range(editor* e, int from, int to) {
  memmove(e->line + from, e->line + to, (size_t)(e->length - to));
  e->length -= to - from;
  e->cursor = from;
}

static void replace(editor* e, const char* text) {
  int length = (int)strlen(text);
  if (length > e->capacity) length = e->capacity;
  memcpy(e->line, text, (size_t)length);
  e->length = e->cursor = length;
}

static bool word_char(char c) { return c != ' ' && c != '\t' && c != '(' && c != ')'; }

/* Keys after the escape byte, folded into the control keys that do the same thing. */
static int escape_key(void) {
  int c = console_read_byte();
  if (c != '[' && c != 'O') return 0;
  c = console_read_byte();
  switch (c) {
  case 'A': return 0x10;
  case 'B': return 0x0e;
  case 'C': return 0x06;
  case 'D': return 0x02;
  case 'H': return 0x01;
  case 'F': return 0x05;
  }
  if (c < '0' || c > '9') return 0;
  int number = 0;
  while (c >= '0' && c <= '9') {
    number = number * 10 + (c - '0');
    c = console_read_byte();
  }
  if (c != '~') return 0;
  if (number == 1 || number == 7) return 0x01;
  if (number == 4 || number == 8) return 0x05;
  if (number == 3) return KEY_DELETE;
  return 0;
}

int line_read(const char* prompt, char* line, int capacity) {
  editor e = {.prompt = prompt, .line = line, .capacity = capacity};
  /* history_count stands for the line being typed, saved in draft while browsing. */
  int browsing = history_count;
  char* draft = NULL;
  put(prompt);
  for (;;) {
    int c = console_read_byte();
    if (c == 0x1b) c = escape_key();
    switch (c) {
    case '\r':
    case '\n':
      free(draft);
      put("\n");
      return e.length;
    case 0x03: /* ^C */
      free(draft);
      put("^C\n");
      return LINE_INTERRUPTED;
    case 0x04: /* ^D */
      if (e.length == 0) {
        free(draft);
        return LINE_EOF;
      }
      [[fallthrough]];
    case KEY_DELETE:
      if (e.cursor < e.length) delete_range(&e, e.cursor, next_char(&e, e.cursor));
      break;
    case 0x7f:
    case 0x08: /* Backspace */
      if (e.cursor > 0) delete_range(&e, previous_char(&e, e.cursor), e.cursor);
      break;
    case 0x01: e.cursor = 0; break;                          /* ^A, Home */
    case 0x05: e.cursor = e.length; break;                   /* ^E, End */
    case 0x02: e.cursor = previous_char(&e, e.cursor); break; /* ^B, Left */
    case 0x06: e.cursor = next_char(&e, e.cursor); break;     /* ^F, Right */
    case 0x0b: e.length = e.cursor; break;                   /* ^K */
    case 0x15: delete_range(&e, 0, e.cursor); break;         /* ^U */
    case 0x17: {                                             /* ^W */
      int start = e.cursor;
      while (start > 0 && !word_char(e.line[start - 1])) start--;
      while (start > 0 && word_char(e.line[start - 1])) start--;
      delete_range(&e, start, e.cursor);
      break;
    }
    case 0x0c: put("\x1b[H\x1b[2J"); break; /* ^L */
    case 0x10:                               /* ^P, Up */
      if (browsing == 0) break;
      if (browsing == history_count) {
        free(draft);
        draft = malloc((size_t)e.length + 1);
        if (draft == NULL) break;
        memcpy(draft, e.line, (size_t)e.length);
        draft[e.length] = '\0';
      }
      replace(&e, history[--browsing]);
      break;
    case 0x0e: /* ^N, Down */
      if (browsing == history_count) break;
      browsing++;
      replace(&e, browsing == history_count ? (draft ? draft : "") : history[browsing]);
      break;
    default: {
      if (c == '\t') c = ' ';
      if (c < 0x20) continue;
      char bytes[4] = {(char)c};
      int count = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
      for (int i = 1; i < count; i++) bytes[i] = (char)console_read_byte();
      if (e.length + count > e.capacity) continue;
      bool at_end = e.cursor == e.length;
      memmove(e.line + e.cursor + count, e.line + e.cursor, (size_t)(e.length - e.cursor));
      memcpy(e.line + e.cursor, bytes, (size_t)count);
      e.cursor += count;
      e.length += count;
      if (at_end) {
        console_write(bytes, (size_t)count);
        continue;
      }
      break;
    }
    }
    redraw(&e);
  }
}
