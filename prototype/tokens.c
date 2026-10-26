#include "internal.h"
#include <stdlib.h>
#include <string.h>

static bool space(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

static bool delimiter(unsigned char c) { return space(c) || strchr("()[]{}\";'`,", c) != NULL; }

static Value pair(Runtime* rt, Value car, Value cdr) {
  Value live[] = {car, cdr};
  Roots roots;
  roots_push(&rt->heap, &roots, live, 2);
  Value result = rt_alloc(rt, K_PAIR, 2, 0);
  if (result) {
    object_fields(result)[0] = live[0];
    object_fields(result)[1] = live[1];
  }
  roots_pop(&rt->heap, &roots);
  return result;
}

static bool validate(Runtime* rt, const char* text, size_t length) {
  size_t position = 0;
  for (;;) {
    if (!runtime_poll_interrupt(rt)) return false;
    Reader r;
    reader_init(&r, text, length, 16 * 1024 * 1024);
    r.position = position;
    Ast* form = reader_next(&r);
    bool done = form == NULL, ok = r.error == NULL;
    position = r.position;
    if (!ok) {
      rt_error(rt, r.error);
      rt->error_offset = r.error_offset;
    }
    reader_destroy(&r);
    if (!ok || done) return ok;
  }
}

static bool advance(Runtime* rt, const char* text, size_t end, size_t* position, size_t* line,
                    size_t* column, bool* cr) {
  while (*position < end) {
    unsigned char c = (unsigned char)text[*position];
    size_t next = rt_utf8_scalar((const unsigned char*)text, *position, end);
    if (!next || !c) {
      rt_error(rt, "invalid source character");
      rt->error_offset = *position;
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

Value rt_tokens(Runtime* rt, size_t first, size_t count) {
  if (count != 1 || !rt_kind(rt->stack[first], K_STRING)) {
    rt_error(rt, "tokens needs one source string");
    return 0;
  }
  size_t length = rt_bytes_length(rt->stack[first]);
  char* text = malloc(length + 1);
  if (!text) {
    rt_error(rt, "out of host memory");
    return 0;
  }
  memcpy(text, rt_bytes_data(rt->stack[first]), length);
  text[length] = 0;
  if (!validate(rt, text, length)) {
    free(text);
    return 0;
  }
  Value live[] = {V_EMPTY, V_NIL, V_NIL, V_NIL, V_NIL};
  Roots roots;
  roots_push(&rt->heap, &roots, live, 5);
  size_t position = 0, line = 0, column = 0, last_line = 0;
  bool cr = false, ok = true;
  while (ok && position < length) {
    if (!runtime_poll_interrupt(rt)) {
      ok = false;
      break;
    }
    size_t end = position;
    while (end < length && space((unsigned char)text[end])) end++;
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
        end = rt_utf8_scalar((unsigned char*)text, start + 2, length);
        if (!end) {
          rt_error(rt, "invalid character token");
          ok = false;
          break;
        }
        if (delimiter((unsigned char)text[start + 2])) goto span;
      }
      while (end < length && !delimiter((unsigned char)text[end])) end++;
    }
  span:
    if (!advance(rt, text, end, &position, &line, &column, &cr)) {
      ok = false;
      break;
    }
    last_line = line;
    live[2] = rt_intern(rt, K_KEYWORD, kind, strlen(kind));
    ok = live[2] != 0;
    if (ok) {
      live[3] = rt_text(rt, K_STRING, text + start, end - start);
      ok = live[3] != 0;
    }
    if (ok) {
      live[4] = pair(rt, value_int((intptr_t)gap), V_EMPTY);
      ok = live[4] != 0;
    }
    if (ok) {
      live[4] = pair(rt, value_int((intptr_t)start_column), live[4]);
      ok = live[4] != 0;
    }
    if (ok) {
      live[4] = pair(rt, value_int((intptr_t)start_line), live[4]);
      ok = live[4] != 0;
    }
    if (ok) {
      live[4] = pair(rt, live[3], live[4]);
      ok = live[4] != 0;
    }
    if (ok) {
      live[4] = pair(rt, live[2], live[4]);
      ok = live[4] != 0;
    }
    if (ok) {
      Value node = pair(rt, live[4], V_EMPTY);
      ok = node != 0;
      if (ok) {
        if (live[0] == V_EMPTY) live[0] = node;
        else object_fields(live[1])[1] = node;
        live[1] = node;
      }
    }
  }
  Value result = ok ? live[0] : 0;
  roots_pop(&rt->heap, &roots);
  free(text);
  return result;
}
