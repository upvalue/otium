// String, symbol, keyword, and mutable-buffer natives. String positions are
// code points over UTF-8 storage; case operations are ASCII-only. Byte access
// goes through ot_string_* reads or copies into C-heap Bufs, so nothing here
// can hold a stale pointer into the GC heap.
#include "../builtins.h"

static Value nat_str(State* vm, u32 base, u32 argc) {
  OT_SCOPE(vm);
  Buf out = {0};
  for (u32 i = 0; i < argc; i++) ot_display(vm, ARG(i), &out);
  Ref r = ot_push(vm);
  ot_make_string_buf(vm, r, &out);
  buf_deinit(&out);
  return ot_ret(vm, r);
}

static Value nat_string_append(State* vm, u32 base, u32 argc) {
  OT_TRY(need_strings(vm, "string-append", base, argc));
  OT_SCOPE(vm);
  Buf out = {0};
  for (u32 i = 0; i < argc; i++) ot_string_copy(vm, ARG(i), 0, ot_string_len(vm, ARG(i)), &out);
  Ref r = ot_push(vm);
  ot_make_string_buf(vm, r, &out);
  buf_deinit(&out);
  return ot_ret(vm, r);
}

static Value nat_string_length(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string-length", argc, 1, 1));
  OT_TRY(need_string(vm, "string-length", ARG(0)));
  return int_v((i64)ot_string_nchars(vm, ARG(0)));
}

static Value nat_substring(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "substring", argc, 2, 3));
  OT_TRY(need_string(vm, "substring", ARG(0)));
  if (ot_tag(vm, ARG(1)) != Tag_Int || (argc == 3 && ot_tag(vm, ARG(2)) != Tag_Int))
    return raise_error(vm, "substring: indices must be ints");
  i64 nchars = (i64)ot_string_nchars(vm, ARG(0));
  i64 start = ot_int(vm, ARG(1));
  i64 end = argc == 3 ? ot_int(vm, ARG(2)) : nchars;
  if (start < 0) start = 0;
  if (start > nchars) start = nchars;
  if (end < 0) end = 0;
  if (end > nchars) end = nchars;
  OT_SCOPE(vm);
  Ref out = ot_push(vm);
  if (end < start) {
    ot_make_string(vm, out, "", 0);
    return ot_ret(vm, out);
  }
  u32 b0 = ot_string_utf8_offset(vm, ARG(0), (u32)start);
  u32 b1 = ot_string_utf8_offset(vm, ARG(0), (u32)end);
  ot_substring(vm, out, ARG(0), b0, b1 - b0);
  return ot_ret(vm, out);
}

static Value nat_string_split(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string-split", argc, 1, 2));
  OT_TRY(need_string(vm, "string-split", ARG(0)));
  OT_SCOPE(vm);
  Ref out = ot_push(vm);
  ot_make_array(vm, out, 8);
  Ref piece = ot_push(vm);
  u32 n = ot_string_len(vm, ARG(0));
  if (argc == 2) {
    OT_TRY(need_string(vm, "string-split", ARG(1)));
    u32 sepLen = ot_string_len(vm, ARG(1));
    if (sepLen == 0) return raise_error(vm, "string-split: empty separator");
    u32 start = 0, at = 0;
    while (ot_string_find(vm, ARG(0), ARG(1), start, &at)) {
      ot_substring(vm, piece, ARG(0), start, at - start);
      ot_array_push(vm, out, piece);
      start = at + sepLen;
    }
    ot_substring(vm, piece, ARG(0), start, n - start);
    ot_array_push(vm, out, piece);
  } else {
    u32 i = 0;
    while (i < n) {
      while (i < n && ascii_whitespace(ot_string_byte(vm, ARG(0), i))) i++;
      u32 start = i;
      while (i < n && !ascii_whitespace(ot_string_byte(vm, ARG(0), i))) i++;
      if (i > start) {
        ot_substring(vm, piece, ARG(0), start, i - start);
        ot_array_push(vm, out, piece);
      }
    }
  }
  return ot_ret(vm, out);
}

static Value nat_string_join(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string-join", argc, 2, 2));
  OT_TRY(need_string(vm, "string-join", ARG(0)));
  OT_SCOPE(vm);
  Ref cursor = ot_push_copy(vm, ARG(1));
  Ref item = ot_push(vm);
  SeqIter iter;
  seq_iter_init(&iter, vm, cursor);
  Buf out = {0};
  bool first = true;
  for (;;) {
    SeqStep step = seq_iter_next(&iter, item);
    if (step == SeqStep_End) break;
    if (step != SeqStep_Item) {
      buf_deinit(&out);
      return sequence_error(vm, "string-join", step);
    }
    if (!first) ot_string_copy(vm, ARG(0), 0, ot_string_len(vm, ARG(0)), &out);
    ot_display(vm, item, &out);
    first = false;
  }
  Ref r = ot_push(vm);
  ot_make_string_buf(vm, r, &out);
  buf_deinit(&out);
  return ot_ret(vm, r);
}

static Value case_op(State* vm, u32 base, u32 argc, const char* who, bool up) {
  OT_TRY(need_argc(vm, who, argc, 1, 1));
  OT_TRY(need_string(vm, who, ARG(0)));
  OT_SCOPE(vm);
  Buf out = {0};
  ot_string_copy(vm, ARG(0), 0, ot_string_len(vm, ARG(0)), &out);
  for (u32 i = 0; i < out.len; i++) {
    char c = out.data[i];
    if (up && c >= 'a' && c <= 'z') out.data[i] = (char)(c - 32);
    if (!up && c >= 'A' && c <= 'Z') out.data[i] = (char)(c + 32);
  }
  Ref r = ot_push(vm);
  ot_make_string_buf(vm, r, &out);
  buf_deinit(&out);
  return ot_ret(vm, r);
}

static Value nat_upcase(State* vm, u32 base, u32 argc) {
  return case_op(vm, base, argc, "string-upcase", true);
}
static Value nat_downcase(State* vm, u32 base, u32 argc) {
  return case_op(vm, base, argc, "string-downcase", false);
}

static Value nat_trim(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string-trim", argc, 1, 1));
  OT_TRY(need_string(vm, "string-trim", ARG(0)));
  u32 a = 0, b = ot_string_len(vm, ARG(0));
  while (a < b && ascii_whitespace(ot_string_byte(vm, ARG(0), a))) a++;
  while (b > a && ascii_whitespace(ot_string_byte(vm, ARG(0), b - 1))) b--;
  OT_SCOPE(vm);
  Ref out = ot_push(vm);
  ot_substring(vm, out, ARG(0), a, b - a);
  return ot_ret(vm, out);
}

// Lexicographic order; byte compare == code-point order for UTF-8.
static Value nat_string_lt(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string<?", argc, 2, 2));
  OT_TRY(need_strings(vm, "string<?", base, argc));
  return bool_v(ot_string_cmp(vm, ARG(0), ARG(1)) < 0);
}

static Value nat_containsp(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string-contains?", argc, 2, 2));
  OT_TRY(need_strings(vm, "string-contains?", base, argc));
  return bool_v(ot_string_find(vm, ARG(0), ARG(1), 0, nullptr));
}

static Value nat_startsp(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string-starts-with?", argc, 2, 2));
  OT_TRY(need_strings(vm, "string-starts-with?", base, argc));
  return bool_v(ot_string_region_eq(vm, ARG(0), 0, ARG(1), 0, ot_string_len(vm, ARG(1))));
}

static Value nat_endsp(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string-ends-with?", argc, 2, 2));
  OT_TRY(need_strings(vm, "string-ends-with?", base, argc));
  u32 sLen = ot_string_len(vm, ARG(0));
  u32 tLen = ot_string_len(vm, ARG(1));
  return bool_v(tLen <= sLen && ot_string_region_eq(vm, ARG(0), sLen - tLen, ARG(1), 0, tLen));
}

static Value nat_replace(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string-replace", argc, 3, 3));
  OT_TRY(need_strings(vm, "string-replace", base, argc));
  u32 fromLen = ot_string_len(vm, ARG(1));
  if (fromLen == 0) return ot_ret(vm, ARG(0));  // nothing to replace
  OT_SCOPE(vm);
  Buf out = {0};
  u32 sLen = ot_string_len(vm, ARG(0));
  u32 toLen = ot_string_len(vm, ARG(2));
  u32 start = 0, at = 0;
  while (ot_string_find(vm, ARG(0), ARG(1), start, &at)) {
    ot_string_copy(vm, ARG(0), start, at - start, &out);
    ot_string_copy(vm, ARG(2), 0, toLen, &out);
    start = at + fromLen;
  }
  ot_string_copy(vm, ARG(0), start, sLen - start, &out);
  Ref r = ot_push(vm);
  ot_make_string_buf(vm, r, &out);
  buf_deinit(&out);
  return ot_ret(vm, r);
}

static Value nat_string_to_number(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string->number", argc, 1, 1));
  OT_TRY(need_string(vm, "string->number", ARG(0)));
  u32 len = ot_string_len(vm, ARG(0));
  char buf[64];
  if (len == 0 || len >= sizeof buf) return nil_v();
  for (u32 i = 0; i < len; i++) buf[i] = (char)ot_string_byte(vm, ARG(0), i);
  buf[len] = 0;
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

static Value nat_number_to_string(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "number->string", base, argc, 1, 1));
  OT_SCOPE(vm);
  Buf out = {0};
  ot_display(vm, ARG(0), &out);
  Ref r = ot_push(vm);
  ot_make_string_buf(vm, r, &out);
  buf_deinit(&out);
  return ot_ret(vm, r);
}

static Value nat_string_to_symbol(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "string->symbol", argc, 1, 1));
  OT_TRY(need_string(vm, "string->symbol", ARG(0)));
  return symbol_v(ot_name_id(vm, ARG(0)));
}

static Value nat_symbol_to_string(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "symbol->string", argc, 1, 1));
  OT_TRY(need_symbol(vm, "symbol->string", ARG(0)));
  u32 len;
  const char* p = ot_intern_name(vm, ot_id(vm, ARG(0)), &len);
  OT_SCOPE(vm);
  Ref r = ot_push(vm);
  ot_make_string(vm, r, p, len);
  return ot_ret(vm, r);
}

// coerce a string/symbol/keyword to an intern id, or return Unwind
static Value coerce_id(State* vm, const char* who, Ref v, u32* out) {
  Tag t = ot_tag(vm, v);
  if (t != Tag_String && t != Tag_Symbol && t != Tag_Keyword)
    return raise_error(vm, "%s: expected string, symbol, or keyword", who);
  *out = ot_name_id(vm, v);
  return nil_v();
}

static Value nat_symbol(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "symbol", argc, 1, 1));
  u32 id = 0;
  OT_TRY(coerce_id(vm, "symbol", ARG(0), &id));
  return symbol_v(id);
}

static Value nat_keyword(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "keyword", argc, 1, 1));
  u32 id = 0;
  OT_TRY(coerce_id(vm, "keyword", ARG(0), &id));
  return keyword_v(id);
}

static Value nat_name(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "name", argc, 1, 1));
  Tag t = ot_tag(vm, ARG(0));
  if (t == Tag_String) return ot_ret(vm, ARG(0));
  if (t == Tag_Symbol || t == Tag_Keyword) {
    u32 len;
    const char* p = ot_intern_name(vm, ot_id(vm, ARG(0)), &len);
    OT_SCOPE(vm);
    Ref r = ot_push(vm);
    ot_make_string(vm, r, p, len);
    return ot_ret(vm, r);
  }
  return raise_error(vm, "name: expected symbol, keyword, or string");
}

static Value nat_buffer(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "buffer", argc, 0, 1));
  OT_SCOPE(vm);
  Ref b = ot_push(vm);
  ot_make_buffer(vm, b);
  if (argc == 1) {
    Buf tmp = {0};
    ot_display(vm, ARG(0), &tmp);
    // tmp is C-heap, so it survives the growth allocation inside the append.
    ot_buffer_append(vm, b, tmp.data ? tmp.data : "", tmp.len);
    buf_deinit(&tmp);
  }
  return ot_ret(vm, b);
}

static Value nat_buffer_push(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "buffer-push!", argc, 1, UINT32_MAX));
  OT_TRY(need_buffer(vm, "buffer-push!", ARG(0)));
  for (u32 i = 1; i < argc; i++) {
    Buf tmp = {0};
    ot_display(vm, ARG(i), &tmp);
    ot_buffer_append(vm, ARG(0), tmp.data ? tmp.data : "", tmp.len);
    buf_deinit(&tmp);
  }
  return ot_ret(vm, ARG(0));
}

static Value nat_buffer_to_string(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "buffer->string", argc, 1, 1));
  OT_TRY(need_buffer(vm, "buffer->string", ARG(0)));
  OT_SCOPE(vm);
  Ref r = ot_push(vm);
  ot_buffer_to_string(vm, r, ARG(0));
  return ot_ret(vm, r);
}

void register_string(State* vm) {
  ot_def_native(vm, "str", nat_str);
  ot_def_native(vm, "string-append", nat_string_append);
  ot_def_native(vm, "string-length", nat_string_length);
  ot_def_native(vm, "substring", nat_substring);
  ot_def_native(vm, "string-split", nat_string_split);
  ot_def_native(vm, "string-join", nat_string_join);
  ot_def_native(vm, "string-upcase", nat_upcase);
  ot_def_native(vm, "string-downcase", nat_downcase);
  ot_def_native(vm, "string-trim", nat_trim);
  ot_def_native(vm, "string<?", nat_string_lt);
  ot_def_native(vm, "string-contains?", nat_containsp);
  ot_def_native(vm, "string-starts-with?", nat_startsp);
  ot_def_native(vm, "string-ends-with?", nat_endsp);
  ot_def_native(vm, "string-replace", nat_replace);
  ot_def_native(vm, "string->number", nat_string_to_number);
  ot_def_native(vm, "number->string", nat_number_to_string);
  ot_def_native(vm, "string->symbol", nat_string_to_symbol);
  ot_def_native(vm, "symbol->string", nat_symbol_to_string);
  ot_def_native(vm, "symbol", nat_symbol);
  ot_def_native(vm, "keyword", nat_keyword);
  ot_def_native(vm, "name", nat_name);
  ot_def_native(vm, "buffer", nat_buffer);
  ot_def_native(vm, "buffer-push!", nat_buffer_push);
  ot_def_native(vm, "buffer->string", nat_buffer_to_string);
}
