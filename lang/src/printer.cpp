#include "printer.hpp"
#include "value.hpp"
#include "heap.hpp"
#include "state.hpp"
#include "code.hpp"
#include <cstring>
#include <cstdio>   // snprintf (deviation from allowed-header list; noted)
#include <cstdlib>  // strtod
#include <cmath>

namespace ot {

__attribute__((weak)) bool printer_table_next(State&, Value, u32*, Value*, Value*) { return false; }

static void print_val(State& vm, Value v, Buf& out, bool display);

static void append_escaped(Buf& out, const char* s, u32 n) {
  for (u32 i = 0; i < n; i++) {
    char c = s[i];
    switch (c) {
      case '\n': out.appendCstr("\\n"); break;
      case '\t': out.appendCstr("\\t"); break;
      case '\r': out.appendCstr("\\r"); break;
      case '\0': out.appendCstr("\\0"); break;
      case '\x1b': out.appendCstr("\\e"); break;
      case '"': out.appendCstr("\\\""); break;
      case '\\': out.appendCstr("\\\\"); break;
      default: out.push(c); break;
    }
  }
}

// Shortest round-tripping float representation; integral values keep ".0".
static void print_float(f64 f, Buf& out) {
  char tmp[40];
  if (std::isnan(f)) {
    out.appendCstr("nan");
    return;
  }
  if (std::isinf(f)) {
    out.appendCstr(f < 0 ? "-inf" : "inf");
    return;
  }
  int len = 0;
  // Integral values stay in decimal notation (spec: `1000.0`, not `1e+03`)
  // while the integer part is exactly representable.
  if (f == std::floor(f) && std::fabs(f) < 1e17) {
    len = snprintf(tmp, sizeof(tmp), "%.1f", f);
    if (strtod(tmp, nullptr) == f) {
      out.append(tmp, (u32)len);
      return;
    }
  }
  for (int prec = 1; prec <= 17; prec++) {
    len = snprintf(tmp, sizeof(tmp), "%.*g", prec, f);
    if (strtod(tmp, nullptr) == f) break;
  }
  out.append(tmp, (u32)len);
  bool hasMark = false;
  for (int i = 0; i < len; i++)
    if (tmp[i] == '.' || tmp[i] == 'e' || tmp[i] == 'E') {
      hasMark = true;
      break;
    }
  if (!hasMark) out.appendCstr(".0");
}

static void print_named(State& vm, const char* kind, u32 nameId, Buf& out) {
  out.appendCstr("#<");
  out.appendCstr(kind);
  if (nameId != 0) {
    out.push(' ');
    u32 n = 0;
    const char* s = vm.intern.name(nameId, &n);
    if (s) out.append(s, n);
  }
  out.push('>');
}

static void print_function(State& vm, Value value, const char* kind, Buf& out) {
  FunctionData* fn = as_function(value);
  if (fn->code.tag != Tag::Code) {
    print_named(vm, kind, fn->name, out);
    return;
  }
  out.appendCstr("#<");
  out.appendCstr(kind);
  if (fn->name) {
    out.push(' ');
    u32 len = 0;
    const char* name = vm.intern.name(fn->name, &len);
    if (name) out.append(name, len);
  }
  out.push(' ');
  code_print_ascii(fn->code, out);
  out.push('>');
}

static void print_val(State& vm, Value v, Buf& out, bool display) {
  switch (v.tag) {
    case Tag::Nil: out.appendCstr("nil"); return;
    case Tag::Null: out.appendCstr("()"); return;
    case Tag::True: out.appendCstr("#t"); return;
    case Tag::False: out.appendCstr("#f"); return;
    case Tag::Int: out.printf("%lld", (long long)v.i); return;
    case Tag::Float: print_float(v.f, out); return;
    case Tag::Symbol: {
      u32 n = 0;
      const char* s = vm.intern.name(v.id, &n);
      if (s) out.append(s, n);
      return;
    }
    case Tag::Keyword: {
      out.push(':');
      u32 n = 0;
      const char* s = vm.intern.name(v.id, &n);
      if (s) out.append(s, n);
      return;
    }
    case Tag::String: {
      StringData* sd = as_string(v);
      if (display) {
        out.append(string_bytes(sd), sd->len);
      } else {
        out.push('"');
        append_escaped(out, string_bytes(sd), sd->len);
        out.push('"');
      }
      return;
    }
    case Tag::Pair: {
      out.push('(');
      Value cur = v;
      bool first = true;
      while (cur.tag == Tag::Pair) {
        if (!first) out.push(' ');
        first = false;
        PairData* p = as_pair(cur);
        print_val(vm, p->car, out, display);
        cur = p->cdr;
      }
      if (cur.tag != Tag::Null) {
        out.appendCstr(" . ");
        print_val(vm, cur, out, display);
      }
      out.push(')');
      return;
    }
    case Tag::Array: {
      ArrayData* a = as_array(v);
      out.push('[');
      for (u32 i = 0; i < a->len; i++) {
        if (i) out.push(' ');
        print_val(vm, a->items[i], out, display);
      }
      out.push(']');
      return;
    }
    case Tag::Table: {
      out.push('{');
      bool first = true;
      u32 cursor = 0;
      Value k, val;
      while (printer_table_next(vm, v, &cursor, &k, &val)) {
        if (!first) out.push(' ');
        first = false;
        print_val(vm, k, out, display);
        out.push(' ');
        print_val(vm, val, out, display);
      }
      out.push('}');
      return;
    }
    case Tag::Buffer: {
      BufferData* b = as_buffer(v);
      if (display) {
        out.append(b->buf.data, b->buf.len);
      } else {
        out.appendCstr("#<buffer \"");
        append_escaped(out, b->buf.data, b->buf.len);
        out.appendCstr("\">");
      }
      return;
    }
    case Tag::Code:
      out.appendCstr("#<code ");
      code_print_ascii(v, out);
      out.push('>');
      return;
    case Tag::Function: print_function(vm, v, "fn", out); return;
    case Tag::Macro: print_function(vm, v, "macro", out); return;
    case Tag::Param: print_named(vm, "param", as_param(v)->name, out); return;
    case Tag::Restart: print_named(vm, "restart", as_restart(v)->name, out); return;
    case Tag::Foreign: {
      const ForeignType* type = vm.heap.foreignType(as_foreign(v)->typeId);
      out.appendCstr("#<");
      if (type) {
        u32 len = 0;
        const char* name = vm.intern.name(type->nameSym, &len);
        if (name) out.append(name, len);
        else out.appendCstr("foreign");
      } else {
        out.appendCstr("foreign");
      }
      out.push('>');
      return;
    }
    case Tag::Unwind: out.appendCstr("#<unwind>"); return;
  }
  out.appendCstr("#<unknown>");
}

void print_repr(State& vm, Value v, Buf& out) { print_val(vm, v, out, false); }
void print_display(State& vm, Value v, Buf& out) { print_val(vm, v, out, true); }

}  // namespace ot
