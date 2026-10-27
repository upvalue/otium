#include "internal.h"
#include <stdlib.h>
#include <string.h>

static bool validate(ot_state* rt, const char* text, size_t length) {
  size_t position = 0;
  for (;;) {
    if (!ot_poll_interrupt(rt)) return false;
    oti_reader r;
    oti_reader_init(&r, text, length, OTI_SOURCE_LIMIT);
    r.position = position;
    oti_ast* form = oti_reader_next(&r);
    bool done = form == NULL, ok = r.error == NULL;
    position = r.position;
    if (!ok) {
      oti_coded_error(rt, r.error_code, r.error);
      rt->error.offset = r.error_offset;
    }
    oti_reader_destroy(&r);
    if (!ok || done) return ok;
  }
}

static bool advance(ot_state* rt, const char* text, size_t end, size_t* position, size_t* line,
                    size_t* column, bool* cr) {
  while (*position < end) {
    unsigned char c = (unsigned char)text[*position];
    size_t next = oti_utf8_scalar((const unsigned char*)text, *position, end);
    if (!next || !c) {
      oti_error(rt, "invalid source character");
      rt->error.offset = *position;
      return false;
    }
    if (c == '\r') {
      (*line)++;
      *column = 0;
    } else if (c == '\n') {
      if (!*cr) (*line)++;
      *column = 0;
    } else (*column)++;
    *cr = c == '\r';
    *position = next;
  }
  return true;
}

ot_value oti_tokens(ot_state* rt, size_t first, size_t count) {
  if (count != 1 || !oti_kind(rt->stack[first], K_STRING)) {
    oti_error(rt, "tokens needs one source string");
    return 0;
  }
  size_t length = oti_bytes_length(rt->stack[first]);
  char* text = malloc(length + 1);
  if (!text) {
    oti_error(rt, "out of host memory");
    return 0;
  }
  memcpy(text, oti_bytes_data(rt->stack[first]), length);
  text[length] = 0;
  if (!validate(rt, text, length)) {
    free(text);
    return 0;
  }
  ot_value live[] = {OT_EMPTY, OT_NIL, OT_NIL, OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 5);
  size_t position = 0, line = 0, column = 0, last_line = 0;
  bool cr = false, ok = true;
  while (ok && position < length) {
    if (!ot_poll_interrupt(rt)) {
      ok = false;
      break;
    }
    size_t end = position;
    while (end < length && oti_reader_space((unsigned char)text[end])) end++;
    if (!advance(rt, text, end, &position, &line, &column, &cr)) {
      ok = false;
      break;
    }
    if (position == length) break;
    size_t start = position, start_line = line, start_column = column, gap = line - last_line;
    const char* kind = ":atom";
    unsigned char c = (unsigned char)text[start];
    end = start + 1;
    if (c == ';') {
      kind = ":comment";
      while (end < length && text[end] != '\r' && text[end] != '\n') end++;
    } else if (c == '"') {
      kind = ":string";
      while (end < length) {
        if (text[end++] == '"') break;
        if (text[end - 1] == '\\' && end < length) end++;
      }
    } else if (strchr("([{", c)) kind = ":open";
    else if (strchr(")]}", c)) kind = ":close";
    else if (strchr("'`,", c)) {
      kind = ":prefix";
      if (c == ',' && end < length && text[end] == '@') end++;
    } else if (length - start >= 2 && c == '#' && text[start + 1] == '`') {
      kind = ":prefix";
      end = start + 2;
    } else if (length - start >= 4 && !memcmp(text + start, "#u8(", 4)) {
      kind = ":open";
      end = start + 4;
    } else {
      if (length - start >= 2 && c == '#' && text[start + 1] == '\\') {
        end = oti_utf8_scalar((unsigned char*)text, start + 2, length);
        if (!end) {
          oti_error(rt, "invalid character token");
          ok = false;
          break;
        }
        if (oti_reader_delimiter((unsigned char)text[start + 2])) goto span;
      }
      while (end < length && !oti_reader_delimiter((unsigned char)text[end])) end++;
    }
  span:
    if (!advance(rt, text, end, &position, &line, &column, &cr)) {
      ok = false;
      break;
    }
    last_line = line;
    live[2] = oti_intern(rt, K_KEYWORD, kind, strlen(kind));
    ok = live[2] != 0;
    if (ok) {
      live[3] = oti_text(rt, K_STRING, text + start, end - start);
      ok = live[3] != 0;
    }
    if (ok) {
      live[4] = oti_cons(rt, ot_int((intptr_t)gap), OT_EMPTY);
      ok = live[4] != 0;
    }
    if (ok) {
      live[4] = oti_cons(rt, ot_int((intptr_t)start_column), live[4]);
      ok = live[4] != 0;
    }
    if (ok) {
      live[4] = oti_cons(rt, ot_int((intptr_t)start_line), live[4]);
      ok = live[4] != 0;
    }
    if (ok) {
      live[4] = oti_cons(rt, live[3], live[4]);
      ok = live[4] != 0;
    }
    if (ok) {
      live[4] = oti_cons(rt, live[2], live[4]);
      ok = live[4] != 0;
    }
    if (ok) {
      ot_value node = oti_cons(rt, live[4], OT_EMPTY);
      ok = node != 0;
      if (ok) {
        if (live[0] == OT_EMPTY) live[0] = node;
        else oti_as_pair(live[1])->cdr = node;
        live[1] = node;
      }
    }
  }
  ot_value result = ok ? live[0] : 0;
  oti_roots_pop(&rt->heap, &roots);
  free(text);
  return result;
}
