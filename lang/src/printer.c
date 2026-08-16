#include "printer.h"
#include "numio.h"

static void print_ref(State* vm, Ref value, Buf* out, bool display);

static void append_escaped(Buf* out, const char* s, u32 n) {
  for (u32 i = 0; i < n; i++) {
    char c = s[i];
    switch (c) {
      case '\n': buf_append_cstr(out, "\\n"); break;
      case '\t': buf_append_cstr(out, "\\t"); break;
      case '\r': buf_append_cstr(out, "\\r"); break;
      case '\0': buf_append_cstr(out, "\\0"); break;
      case '\x1b': buf_append_cstr(out, "\\e"); break;
      case '"': buf_append_cstr(out, "\\\""); break;
      case '\\': buf_append_cstr(out, "\\\\"); break;
      default: vec_push(out, c); break;
    }
  }
}

static void print_float(f64 f, Buf* out) {
  char tmp[40];
  u32 len = num_format_f64(f, tmp, sizeof(tmp));
  buf_append(out, tmp, len);
}

static void print_named(State* vm, const char* kind, u32 nameId, Buf* out) {
  buf_append_cstr(out, "#<");
  buf_append_cstr(out, kind);
  if (nameId != 0) {
    if (*kind) vec_push(out, ' ');
    u32 n = 0;
    const char* s = ot_intern_name(vm, nameId, &n);
    if (s) buf_append(out, s, n);
  }
  vec_push(out, '>');
}

static void print_function(State* vm, Ref value, const char* kind, Buf* out) {
  OT_SCOPE(vm);
  u32 name = ot_callable_name(vm, value);
  Ref code = ot_push(vm);
  if (!ot_callable_code(vm, code, value)) {
    print_named(vm, kind, name, out);
    return;
  }
  buf_append_cstr(out, "#<");
  buf_append_cstr(out, kind);
  if (name) {
    vec_push(out, ' ');
    u32 len = 0;
    const char* text = ot_intern_name(vm, name, &len);
    if (text) buf_append(out, text, len);
  }
  vec_push(out, ' ');
  ot_code_ascii(vm, code, out);
  vec_push(out, '>');
}

static void print_bytes(State* vm, Ref value, Buf* out, bool display, bool buffer) {
  Buf bytes = {0};
  if (buffer) ot_buffer_copy(vm, value, &bytes);
  else ot_string_copy(vm, value, 0, ot_string_len(vm, value), &bytes);
  if (display) buf_append(out, bytes.data, bytes.len);
  else {
    if (buffer) buf_append_cstr(out, "#<buffer \"");
    else vec_push(out, '"');
    append_escaped(out, bytes.data, bytes.len);
    if (buffer) buf_append_cstr(out, "\">");
    else vec_push(out, '"');
  }
  buf_deinit(&bytes);
}

static void print_ref(State* vm, Ref value, Buf* out, bool display) {
  switch (ot_tag(vm, value)) {
    case Tag_Nil: buf_append_cstr(out, "nil"); return;
    case Tag_Null: buf_append_cstr(out, "()"); return;
    case Tag_True: buf_append_cstr(out, "#t"); return;
    case Tag_False: buf_append_cstr(out, "#f"); return;
    case Tag_Int: {
      char tmp[24];
      u32 n = num_format_i64(ot_int(vm, value), tmp, sizeof(tmp));
      buf_append(out, tmp, n);
      return;
    }
    case Tag_Float: print_float(ot_float(vm, value), out); return;
    case Tag_Symbol: {
      u32 n = 0;
      const char* s = ot_intern_name(vm, ot_id(vm, value), &n);
      if (s) buf_append(out, s, n);
      return;
    }
    case Tag_Keyword: {
      vec_push(out, ':');
      u32 n = 0;
      const char* s = ot_intern_name(vm, ot_id(vm, value), &n);
      if (s) buf_append(out, s, n);
      return;
    }
    case Tag_String: print_bytes(vm, value, out, display, false); return;
    case Tag_Pair: {
      OT_SCOPE(vm);
      Ref cursor = ot_push_copy(vm, value);
      Ref item = ot_push(vm);
      vec_push(out, '(');
      bool first = true;
      while (ot_tag(vm, cursor) == Tag_Pair) {
        if (!first) vec_push(out, ' ');
        first = false;
        ot_car(vm, item, cursor);
        print_ref(vm, item, out, display);
        ot_cdr(vm, cursor, cursor);
      }
      if (ot_tag(vm, cursor) != Tag_Null) {
        buf_append_cstr(out, " . ");
        print_ref(vm, cursor, out, display);
      }
      vec_push(out, ')');
      return;
    }
    case Tag_Array: {
      OT_SCOPE(vm);
      Ref item = ot_push(vm);
      vec_push(out, '[');
      for (u32 i = 0; i < ot_array_len(vm, value); i++) {
        if (i) vec_push(out, ' ');
        ot_array_get(vm, item, value, i);
        print_ref(vm, item, out, display);
      }
      vec_push(out, ']');
      return;
    }
    case Tag_Table: {
      OT_SCOPE(vm);
      Ref key = ot_push(vm);
      Ref item = ot_push(vm);
      vec_push(out, '{');
      bool first = true;
      u32 cursor = 0;
      while (ot_table_next(vm, value, &cursor, key, item)) {
        if (!first) vec_push(out, ' ');
        first = false;
        print_ref(vm, key, out, display);
        vec_push(out, ' ');
        print_ref(vm, item, out, display);
      }
      vec_push(out, '}');
      return;
    }
    case Tag_Buffer: print_bytes(vm, value, out, display, true); return;
    case Tag_Code:
      buf_append_cstr(out, "#<code ");
      ot_code_ascii(vm, value, out);
      vec_push(out, '>');
      return;
    case Tag_Function: print_function(vm, value, "fn", out); return;
    case Tag_Macro: print_function(vm, value, "macro", out); return;
    case Tag_Param: print_named(vm, "param", ot_param_name(vm, value), out); return;
    case Tag_Restart: print_named(vm, "restart", ot_restart_name(vm, value), out); return;
    case Tag_Foreign: {
      u32 name = ot_foreign_name(vm, value);
      print_named(vm, name ? "" : "foreign", name, out);
      return;
    }
    case Tag_Unwind: buf_append_cstr(out, "#<unwind>"); return;
  }
  buf_append_cstr(out, "#<unknown>");
}

void print_ref_repr(State* vm, Ref v, Buf* out) { print_ref(vm, v, out, false); }
void print_ref_display(State* vm, Ref v, Buf* out) { print_ref(vm, v, out, true); }
