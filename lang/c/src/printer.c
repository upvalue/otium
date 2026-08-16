#include "printer.h"
#include "builtins.h"
#include "value.h"
#include "heap.h"
#include "state.h"
#include "code.h"
#include "numio.h"

static void print_val(State* vm, Value v, Buf* out, bool display);

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

// Shortest round-tripping float representation; integral values keep ".0"
// (the formatting itself lives behind the numio seam).
static void print_float(f64 f, Buf* out) {
  char tmp[40];
  u32 len = num_format_f64(f, tmp, sizeof(tmp));
  buf_append(out, tmp, len);
}

static void print_named(State* vm, const char* kind, u32 nameId, Buf* out) {
  buf_append_cstr(out, "#<");
  buf_append_cstr(out, kind);
  if (nameId != 0) {
    vec_push(out, ' ');
    u32 n = 0;
    const char* s = intern_name(&vm->intern, nameId, &n);
    if (s) buf_append(out, s, n);
  }
  vec_push(out, '>');
}

static void print_function(State* vm, Value value, const char* kind, Buf* out) {
  FunctionData* fn = as_function(value);
  if (fn->code.tag != Tag_Code) {
    print_named(vm, kind, fn->name, out);
    return;
  }
  buf_append_cstr(out, "#<");
  buf_append_cstr(out, kind);
  if (fn->name) {
    vec_push(out, ' ');
    u32 len = 0;
    const char* name = intern_name(&vm->intern, fn->name, &len);
    if (name) buf_append(out, name, len);
  }
  vec_push(out, ' ');
  code_print_ascii(fn->code, out);
  vec_push(out, '>');
}

static void print_val(State* vm, Value v, Buf* out, bool display) {
  switch (v.tag) {
    case Tag_Nil: buf_append_cstr(out, "nil"); return;
    case Tag_Null: buf_append_cstr(out, "()"); return;
    case Tag_True: buf_append_cstr(out, "#t"); return;
    case Tag_False: buf_append_cstr(out, "#f"); return;
    case Tag_Int: {
      char tmp[24];
      u32 n = num_format_i64(v.i, tmp, sizeof(tmp));
      buf_append(out, tmp, n);
      return;
    }
    case Tag_Float: print_float(v.f, out); return;
    case Tag_Symbol: {
      u32 n = 0;
      const char* s = intern_name(&vm->intern, v.id, &n);
      if (s) buf_append(out, s, n);
      return;
    }
    case Tag_Keyword: {
      vec_push(out, ':');
      u32 n = 0;
      const char* s = intern_name(&vm->intern, v.id, &n);
      if (s) buf_append(out, s, n);
      return;
    }
    case Tag_String: {
      StringData* sd = as_string(v);
      if (display) {
        buf_append(out, string_data_bytes(sd), sd->len);
      } else {
        vec_push(out, '"');
        append_escaped(out, string_data_bytes(sd), sd->len);
        vec_push(out, '"');
      }
      return;
    }
    case Tag_Pair: {
      vec_push(out, '(');
      Value cur = v;
      bool first = true;
      while (cur.tag == Tag_Pair) {
        if (!first) vec_push(out, ' ');
        first = false;
        PairData* p = as_pair(cur);
        print_val(vm, p->car, out, display);
        cur = p->cdr;
      }
      if (cur.tag != Tag_Null) {
        buf_append_cstr(out, " . ");
        print_val(vm, cur, out, display);
      }
      vec_push(out, ')');
      return;
    }
    case Tag_Array: {
      ArrayData* a = as_array(v);
      vec_push(out, '[');
      for (u32 i = 0; i < a->len; i++) {
        if (i) vec_push(out, ' ');
        print_val(vm, a->items[i], out, display);
      }
      vec_push(out, ']');
      return;
    }
    case Tag_Table: {
      vec_push(out, '{');
      bool first = true;
      u32 cursor = 0;
      Value k, val;
      while (table_iter_next(v, &cursor, &k, &val)) {
        if (!first) vec_push(out, ' ');
        first = false;
        print_val(vm, k, out, display);
        vec_push(out, ' ');
        print_val(vm, val, out, display);
      }
      vec_push(out, '}');
      return;
    }
    case Tag_Buffer: {
      BufferData* b = as_buffer(v);
      if (display) {
        buf_append(out, b->buf.data, b->buf.len);
      } else {
        buf_append_cstr(out, "#<buffer \"");
        append_escaped(out, b->buf.data, b->buf.len);
        buf_append_cstr(out, "\">");
      }
      return;
    }
    case Tag_Code:
      buf_append_cstr(out, "#<code ");
      code_print_ascii(v, out);
      vec_push(out, '>');
      return;
    case Tag_Function: print_function(vm, v, "fn", out); return;
    case Tag_Macro: print_function(vm, v, "macro", out); return;
    case Tag_Param: print_named(vm, "param", as_param(v)->name, out); return;
    case Tag_Restart: print_named(vm, "restart", as_restart(v)->name, out); return;
    case Tag_Foreign: {
      const ForeignType* type = heap_foreign_type(&vm->heap, as_foreign(v)->typeId);
      buf_append_cstr(out, "#<");
      if (type) {
        u32 len = 0;
        const char* name = intern_name(&vm->intern, type->nameSym, &len);
        if (name) buf_append(out, name, len);
        else buf_append_cstr(out, "foreign");
      } else {
        buf_append_cstr(out, "foreign");
      }
      vec_push(out, '>');
      return;
    }
    case Tag_Unwind: buf_append_cstr(out, "#<unwind>"); return;
  }
  buf_append_cstr(out, "#<unknown>");
}

void print_repr(State* vm, Value v, Buf* out) { print_val(vm, v, out, false); }
void print_display(State* vm, Value v, Buf* out) { print_val(vm, v, out, true); }
