// String, symbol, keyword, and mutable-buffer natives. String positions are
// code points over UTF-8 storage; case operations are ASCII-only.
#include "../builtins.hpp"
#include "../vm.hpp"
#include "../ns.hpp"
#include "../heap.hpp"
#include "../printer.hpp"
#include "../intern.hpp"
#include "../sequence.hpp"
#include <cmath>

namespace ot {

// --- small UTF-8 helpers (positions in code points) ------------------------

static bool is_cont(u8 c) { return (c & 0xC0) == 0x80; }

// byte offset of the n-th code point (clamped to [0, len])
static u32 utf8_offset(const char* p, u32 len, u32 n) {
  u32 i = 0, c = 0;
  while (i < len && c < n) {
    i++;
    while (i < len && is_cont((u8)p[i])) i++;
    c++;
  }
  return i;
}

// --- natives ----------------------------------------------------------------

static Value nat_str(Vm& vm, u32 base, u32 argc) {
  Buf out;
  for (u32 i = 0; i < argc; i++) print_display(vm, ARG(i), out);
  return make_string(vm, out);
}

static Value nat_string_append(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_strings(vm, "string-append", base, argc));
  Buf out;
  for (u32 i = 0; i < argc; i++) {
    StringData* s = as_string(ARG(i));
    out.append(string_bytes(s), s->len);
  }
  return make_string(vm, out);
}

static Value nat_string_length(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string-length", argc, 1, 1));
  OT_TRY(need_string(vm, "string-length", ARG(0)));
  return int_v((i64)as_string(ARG(0))->nchars);
}

static Value nat_substring(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "substring", argc, 2, 3));
  OT_TRY(need_string(vm, "substring", ARG(0)));
  if (ARG(1).tag != Tag::Int || (argc == 3 && ARG(2).tag != Tag::Int))
    return raise_error(vm, "substring: indices must be ints");
  StringData* s = as_string(ARG(0));
  i64 nchars = (i64)s->nchars;
  i64 start = ARG(1).i, end = argc == 3 ? ARG(2).i : nchars;
  if (start < 0) start = 0;
  if (start > nchars) start = nchars;
  if (end < 0) end = 0;
  if (end > nchars) end = nchars;
  if (end < start) return make_string(vm, "", 0);
  u32 b0 = utf8_offset(string_bytes(s), s->len, (u32)start);
  u32 b1 = utf8_offset(string_bytes(s), s->len, (u32)end);
  return make_string_from(vm, ARG(0), b0, b1 - b0);
}

static Value nat_string_split(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string-split", argc, 1, 2));
  OT_TRY(need_string(vm, "string-split", ARG(0)));
  Scope scope(vm);
  Slot out = scope.push(make_array(vm, 8));
  // Fetch source pointers only after the array alloc above — and re-fetch
  // after every subsequent allocation.
  StringData* s = as_string(ARG(0));
  const char* p = string_bytes(s);
  u32 n = s->len;
  if (argc == 2) {
    OT_TRY(need_string(vm, "string-split", ARG(1)));
    StringData* sep = as_string(ARG(1));
    if (sep->len == 0) return raise_error(vm, "string-split: empty separator");
    // re-fetch pointers after each allocation: make_string_from may GC-move
    // objects (it roots the source internally; out is re-read from its slot).
    u32 start = 0, i = 0;
    while (i + sep->len <= n) {
      if (memcmp(p + i, string_bytes(sep), sep->len) == 0) {
        Value piece = make_string_from(vm, ARG(0), start, i - start);
        array_push(vm, out.get(), piece);
        s = as_string(ARG(0));
        sep = as_string(ARG(1));
        p = string_bytes(s);  // re-fetch
        i += sep->len;
        start = i;
      } else i++;
    }
    Value piece = make_string_from(vm, ARG(0), start, n - start);
    array_push(vm, out.get(), piece);
  } else {
    u32 i = 0;
    while (i < n) {
      while (i < n && ascii_whitespace((u8)p[i])) i++;
      u32 start = i;
      while (i < n && !ascii_whitespace((u8)p[i])) i++;
      if (i > start) {
        Value piece = make_string_from(vm, ARG(0), start, i - start);
        array_push(vm, out.get(), piece);
        s = as_string(ARG(0));
        p = string_bytes(s);  // re-fetch after alloc
      }
    }
  }
  return out.get();
}

static Value nat_string_join(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string-join", argc, 2, 2));
  OT_TRY(need_string(vm, "string-join", ARG(0)));
  Scope roots(vm);
  Slot cursor = roots.push(ARG(1));
  Slot item = roots.push();
  SeqIter iter(cursor);
  Buf out;
  bool first = true;
  for (;;) {
    SeqStep step = iter.next(item);
    if (step == SeqStep::End) break;
    if (step != SeqStep::Item) return sequence_error(vm, "string-join", step);
    if (!first) {
      StringData* sep = as_string(ARG(0));
      out.append(string_bytes(sep), sep->len);
    }
    print_display(vm, item.get(), out);
    first = false;
  }
  return make_string(vm, out);
}

static Value case_op(Vm& vm, u32 base, u32 argc, const char* who, bool up) {
  OT_TRY(need_argc(vm, who, argc, 1, 1));
  OT_TRY(need_string(vm, who, ARG(0)));
  StringData* s = as_string(ARG(0));
  Buf out;
  out.append(string_bytes(s), s->len);
  for (u32 i = 0; i < out.len; i++) {
    char c = out.data[i];
    if (up && c >= 'a' && c <= 'z') out.data[i] = (char)(c - 32);
    if (!up && c >= 'A' && c <= 'Z') out.data[i] = (char)(c + 32);
  }
  return make_string(vm, out);
}

static Value nat_upcase(Vm& vm, u32 base, u32 argc) {
  return case_op(vm, base, argc, "string-upcase", true);
}
static Value nat_downcase(Vm& vm, u32 base, u32 argc) {
  return case_op(vm, base, argc, "string-downcase", false);
}

static Value nat_trim(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string-trim", argc, 1, 1));
  OT_TRY(need_string(vm, "string-trim", ARG(0)));
  StringData* s = as_string(ARG(0));
  const char* p = string_bytes(s);
  u32 a = 0, b = s->len;
  while (a < b && ascii_whitespace((u8)p[a])) a++;
  while (b > a && ascii_whitespace((u8)p[b - 1])) b--;
  return make_string_from(vm, ARG(0), a, b - a);
}

static bool bytes_find(const char* hay, u32 hn, const char* nee, u32 nn, u32* at) {
  if (nn > hn) return false;
  for (u32 i = 0; i + nn <= hn; i++)
    if (memcmp(hay + i, nee, nn) == 0) {
      if (at) *at = i;
      return true;
    }
  return false;
}

// Lexicographic order; byte compare == code-point order for UTF-8.
static Value nat_string_lt(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string<?", argc, 2, 2));
  OT_TRY(need_strings(vm, "string<?", base, argc));
  StringData* a = as_string(ARG(0));
  StringData* b = as_string(ARG(1));
  u32 n = a->len < b->len ? a->len : b->len;
  int c = memcmp(string_bytes(a), string_bytes(b), n);
  return bool_v(c < 0 || (c == 0 && a->len < b->len));
}

static Value nat_containsp(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string-contains?", argc, 2, 2));
  OT_TRY(need_strings(vm, "string-contains?", base, argc));
  StringData* s = as_string(ARG(0));
  StringData* t = as_string(ARG(1));
  return bool_v(bytes_find(string_bytes(s), s->len, string_bytes(t), t->len, nullptr));
}

static Value nat_startsp(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string-starts-with?", argc, 2, 2));
  OT_TRY(need_strings(vm, "string-starts-with?", base, argc));
  StringData* s = as_string(ARG(0));
  StringData* t = as_string(ARG(1));
  return bool_v(t->len <= s->len && memcmp(string_bytes(s), string_bytes(t), t->len) == 0);
}

static Value nat_endsp(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string-ends-with?", argc, 2, 2));
  OT_TRY(need_strings(vm, "string-ends-with?", base, argc));
  StringData* s = as_string(ARG(0));
  StringData* t = as_string(ARG(1));
  return bool_v(t->len <= s->len &&
                memcmp(string_bytes(s) + (s->len - t->len), string_bytes(t), t->len) == 0);
}

static Value nat_replace(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string-replace", argc, 3, 3));
  OT_TRY(need_strings(vm, "string-replace", base, argc));
  StringData* s = as_string(ARG(0));
  StringData* from = as_string(ARG(1));
  StringData* to = as_string(ARG(2));
  if (from->len == 0) return ARG(0);  // nothing to replace
  Buf out;
  const char* p = string_bytes(s);
  u32 i = 0;
  while (i < s->len) {
    if (i + from->len <= s->len && memcmp(p + i, string_bytes(from), from->len) == 0) {
      out.append(string_bytes(to), to->len);
      i += from->len;
    } else {
      out.push(p[i++]);
    }
  }
  return make_string(vm, out);
}

static Value nat_string_to_number(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string->number", argc, 1, 1));
  OT_TRY(need_string(vm, "string->number", ARG(0)));
  StringData* s = as_string(ARG(0));
  char buf[64];
  if (s->len == 0 || s->len >= sizeof buf) return nil_v();
  memcpy(buf, string_bytes(s), s->len);
  buf[s->len] = 0;
  char* q = buf;
  while (*q && ascii_whitespace((u8)*q)) q++;
  if (!*q) return nil_v();
  char* endp = nullptr;
  // int first (overflow clamps per strtoll; fine for a POC)
  long long iv = strtoll(q, &endp, 10);
  if (endp != q) {
    char* r = endp;
    while (*r && ascii_whitespace((u8)*r)) r++;
    if (!*r) return int_v((i64)iv);
  }
  // then float
  f64 fv = strtod(q, &endp);
  if (endp != q) {
    char* r = endp;
    while (*r && ascii_whitespace((u8)*r)) r++;
    if (!*r) return float_v(fv);
  }
  return nil_v();
}

static Value nat_number_to_string(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "number->string", argc, 1, 1));
  OT_TRY(need_nums(vm, "number->string", base, argc));
  Buf out;
  print_display(vm, ARG(0), out);
  return make_string(vm, out);
}

static Value nat_string_to_symbol(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string->symbol", argc, 1, 1));
  OT_TRY(need_string(vm, "string->symbol", ARG(0)));
  StringData* s = as_string(ARG(0));
  return symbol_v(vm.intern.intern(string_bytes(s), s->len));
}

static Value nat_symbol_to_string(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "symbol->string", argc, 1, 1));
  OT_TRY(need_symbol(vm, "symbol->string", ARG(0)));
  u32 len;
  const char* p = vm.intern.name(ARG(0).id, &len);
  return make_string(vm, p, len);
}

// coerce a string/symbol/keyword to an intern id, or return Unwind
static Value coerce_id(Vm& vm, const char* who, Value v, u32* out) {
  switch (v.tag) {
    case Tag::Symbol:
    case Tag::Keyword: *out = v.id; return nil_v();
    case Tag::String: {
      StringData* s = as_string(v);
      *out = vm.intern.intern(string_bytes(s), s->len);
      return nil_v();
    }
    default: return raise_error(vm, "%s: expected string, symbol, or keyword", who);
  }
}

static Value nat_symbol(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "symbol", argc, 1, 1));
  u32 id = 0;
  OT_TRY(coerce_id(vm, "symbol", ARG(0), &id));
  return symbol_v(id);
}

static Value nat_keyword(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "keyword", argc, 1, 1));
  u32 id = 0;
  OT_TRY(coerce_id(vm, "keyword", ARG(0), &id));
  return keyword_v(id);
}

static Value nat_name(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "name", argc, 1, 1));
  Value v = ARG(0);
  if (v.tag == Tag::String) return v;
  if (v.tag == Tag::Symbol || v.tag == Tag::Keyword) {
    u32 len;
    const char* p = vm.intern.name(v.id, &len);
    return make_string(vm, p, len);
  }
  return raise_error(vm, "name: expected symbol, keyword, or string");
}

static Value nat_buffer(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "buffer", argc, 0, 1));
  Scope scope(vm);
  Slot b = scope.push(make_buffer(vm));
  if (argc == 1) {
    Buf tmp;
    print_display(vm, ARG(0), tmp);
    as_buffer(b.get())->buf.append(tmp.data ? tmp.data : "", tmp.len);
  }
  return b.get();
}

static Value nat_buffer_push(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "buffer-push!", argc, 1, UINT32_MAX));
  OT_TRY(need_buffer(vm, "buffer-push!", ARG(0)));
  for (u32 i = 1; i < argc; i++) {
    Buf tmp;
    print_display(vm, ARG(i), tmp);
    as_buffer(ARG(0))->buf.append(tmp.data ? tmp.data : "", tmp.len);
  }
  return ARG(0);
}

static Value nat_buffer_to_string(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "buffer->string", argc, 1, 1));
  OT_TRY(need_buffer(vm, "buffer->string", ARG(0)));
  BufferData* b = as_buffer(ARG(0));
  return make_string(vm, b->buf);
}

void register_string(Vm& vm) {
  def_native(vm, "str", nat_str);
  def_native(vm, "string-append", nat_string_append);
  def_native(vm, "string-length", nat_string_length);
  def_native(vm, "substring", nat_substring);
  def_native(vm, "string-split", nat_string_split);
  def_native(vm, "string-join", nat_string_join);
  def_native(vm, "string-upcase", nat_upcase);
  def_native(vm, "string-downcase", nat_downcase);
  def_native(vm, "string-trim", nat_trim);
  def_native(vm, "string<?", nat_string_lt);
  def_native(vm, "string-contains?", nat_containsp);
  def_native(vm, "string-starts-with?", nat_startsp);
  def_native(vm, "string-ends-with?", nat_endsp);
  def_native(vm, "string-replace", nat_replace);
  def_native(vm, "string->number", nat_string_to_number);
  def_native(vm, "number->string", nat_number_to_string);
  def_native(vm, "string->symbol", nat_string_to_symbol);
  def_native(vm, "symbol->string", nat_symbol_to_string);
  def_native(vm, "symbol", nat_symbol);
  def_native(vm, "keyword", nat_keyword);
  def_native(vm, "name", nat_name);
  def_native(vm, "buffer", nat_buffer);
  def_native(vm, "buffer-push!", nat_buffer_push);
  def_native(vm, "buffer->string", nat_buffer_to_string);
}

}  // namespace ot
