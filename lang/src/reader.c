#include "reader.h"
#include "numio.h"

// Characters that terminate an atom (spec 1.2): ( ) [ ] { } ; " , ' `
static bool is_delim(char c) {
  switch (c) {
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
    case ';':
    case '"':
    case ',':
    case '\'':
    case '`': return true;
    default: return false;
  }
}

static bool atom_end(char c) { return ascii_whitespace((u8)c) || is_delim(c); }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void reader_init(Reader* r, State* vm, const char* src, u32 len, const char* filename) {
  r->vm = vm;
  r->src = src;
  r->len = len;
  r->filename = filename;
  r->pos = 0;
  r->line = 1;
  r->col = 1;
  r->eofFlag = false;
  r->incompleteFlag = false;
}

static bool eof(const Reader* r) { return r->pos >= r->len; }
static char peek(const Reader* r) { return r->src[r->pos]; }

static void advance(Reader* r) {
  if (r->pos >= r->len) return;
  if (r->src[r->pos] == '\n') {
    r->line++;
    r->col = 1;
  } else {
    r->col++;
  }
  r->pos++;
}

static void skip_ws(Reader* r) {
  while (!eof(r)) {
    char c = peek(r);
    if (ascii_whitespace((u8)c)) {
      advance(r);
      continue;
    }
    if (c == ';') {
      while (!eof(r) && peek(r) != '\n') advance(r);
      continue;
    }
    break;
  }
}

static Value err(Reader* r, u32 line, u32 col, const char* what) {
  return raise_error(r->vm, "read error at %u:%u: %s", line, col, what);
}

static Value need_more(Reader* r, u32 line, u32 col, const char* what) {
  r->incompleteFlag = true;
  return err(r, line, col, what);
}

static Value read_form(Reader* r, Ref dst);

// Reads elements up to `close`. Collection literals are represented as a
// proper list headed by ctorSym. Every partial form stays in a stack slot
// while later elements allocate.
static Value read_list(Reader* r, Ref dst, char close, u32 openLine, u32 openCol,
                       const char* ctorSym) {
  State* vm = r->vm;
  OT_SCOPE(vm);
  u32 base = ot_top(vm);
  u32 count = 0;
  bool haveTail = false;
  Ref tail = {0};

  for (;;) {
    skip_ws(r);
    if (eof(r)) return need_more(r, openLine, openCol, "unterminated list");
    char c = peek(r);
    if (c == close) {
      advance(r);
      break;
    }
    if (c == ')' || c == ']' || c == '}')
      return err(r, r->line, r->col, "mismatched closing delimiter");
    if (c == '.' && (r->pos + 1 >= r->len || atom_end(r->src[r->pos + 1]))) {
      u32 dotLine = r->line, dotCol = r->col;
      advance(r);
      if (count == 0) return err(r, dotLine, dotCol, "dotted tail with no preceding element");
      if (close != ')')
        return err(r, dotLine, dotCol, "dotted tail not allowed in collection literal");
      tail = ot_push(vm);
      OT_TRY(read_form(r, tail));
      haveTail = true;
      skip_ws(r);
      if (eof(r)) return need_more(r, r->line, r->col, "expected ) after dotted tail");
      if (peek(r) != close) return err(r, r->line, r->col, "expected ) after dotted tail");
      advance(r);
      break;
    }

    Ref item = ot_push(vm);
    OT_TRY(read_form(r, item));
    count++;
  }

  Ref acc = ot_push(vm);
  if (haveTail) ot_copy(vm, acc, tail);
  else ot_set_null(vm, acc);
  ot_list_from_stack(vm, acc, base, count, acc);
  if (ctorSym) {
    Ref head = ot_push(vm);
    ot_set_symbol(vm, head, ot_intern(vm, ctorSym, (u32)strlen(ctorSym)));
    ot_cons(vm, acc, head, acc);
  }
  ot_copy(vm, dst, acc);
  return nil_v();
}

static Value read_string(Reader* r, Ref dst, u32 openLine, u32 openCol) {
  Buf out = {0};
  for (;;) {
    if (eof(r)) {
      buf_deinit(&out);
      return need_more(r, openLine, openCol, "unterminated string");
    }
    char c = peek(r);
    advance(r);
    if (c == '"') break;
    if (c == '\\') {
      if (eof(r)) {
        buf_deinit(&out);
        return need_more(r, openLine, openCol, "unterminated string");
      }
      char escaped = peek(r);
      u32 line = r->line, col = r->col;
      advance(r);
      switch (escaped) {
        case 'n': vec_push(&out, '\n'); break;
        case 't': vec_push(&out, '\t'); break;
        case 'r': vec_push(&out, '\r'); break;
        case '0': vec_push(&out, '\0'); break;
        case 'e': vec_push(&out, '\x1b'); break;
        case '"': vec_push(&out, '"'); break;
        case '\\': vec_push(&out, '\\'); break;
        default:
          buf_deinit(&out);
          return err(r, line, col, "unknown string escape");
      }
      continue;
    }
    vec_push(&out, c);
  }
  ot_make_string_buf(r->vm, dst, &out);
  buf_deinit(&out);
  return nil_v();
}

static Value read_sugar(Reader* r, Ref dst, const char* sym, u32 symLen) {
  State* vm = r->vm;
  OT_SCOPE(vm);
  Ref inner = ot_push(vm);
  Ref empty = ot_push(vm);
  Ref wrapped = ot_push(vm);
  Ref head = ot_push(vm);
  OT_TRY(read_form(r, inner));
  ot_set_null(vm, empty);
  ot_cons(vm, wrapped, inner, empty);
  ot_set_symbol(vm, head, ot_intern(vm, sym, symLen));
  ot_cons(vm, dst, head, wrapped);
  return nil_v();
}

static Value parse_number(Reader* r, Ref dst, const char* tok, u32 n, u32 line, u32 col) {
  u32 i = 0;
  bool neg = false;
  if (tok[i] == '+' || tok[i] == '-') {
    neg = tok[i] == '-';
    i++;
  }

  if (n - i > 2 && tok[i] == '0' && (tok[i + 1] == 'x' || tok[i + 1] == 'X')) {
    u64 acc = 0;
    for (u32 j = i + 2; j < n; j++) {
      int h = hex_val(tok[j]);
      if (h < 0) return err(r, line, col, "invalid hexadecimal literal");
      if (acc > (u64)0x0FFFFFFFFFFFFFFFull)
        return err(r, line, col, "integer literal out of range");
      acc = acc * 16 + (u64)h;
    }
    u64 limit = neg ? (u64)1 << 63 : ((u64)1 << 63) - 1;
    if (acc > limit) return err(r, line, col, "integer literal out of range");
    ot_set_int(r->vm, dst, neg ? (acc == ((u64)1 << 63) ? INT64_MIN : -(i64)acc) : (i64)acc);
    return nil_v();
  }

  bool isFloat = false;
  for (u32 j = i; j < n; j++)
    if (tok[j] == '.' || tok[j] == 'e' || tok[j] == 'E') {
      isFloat = true;
      break;
    }

  if (!isFloat) {
    u64 acc = 0;
    for (u32 j = i; j < n; j++) {
      if (!is_digit(tok[j])) return err(r, line, col, "invalid number");
      u32 digit = (u32)(tok[j] - '0');
      if (acc > (UINT64_MAX - digit) / 10)
        return err(r, line, col, "integer literal out of range");
      acc = acc * 10 + digit;
    }
    u64 limit = neg ? (u64)1 << 63 : ((u64)1 << 63) - 1;
    if (acc > limit) return err(r, line, col, "integer literal out of range");
    ot_set_int(r->vm, dst, neg ? (acc == ((u64)1 << 63) ? INT64_MIN : -(i64)acc) : (i64)acc);
    return nil_v();
  }

  if (n >= 64) return err(r, line, col, "number literal too long");
  f64 value = 0;
  if (!num_parse_f64(tok, n, &value)) return err(r, line, col, "invalid number");
  ot_set_float(r->vm, dst, value);
  return nil_v();
}

static bool tok_eq(const char* tok, u32 n, const char* lit) {
  u32 len = (u32)strlen(lit);
  return n == len && memcmp(tok, lit, n) == 0;
}

static bool starts_numerically(const char* tok, u32 n) {
  u32 i = 0;
  if (i < n && (tok[i] == '+' || tok[i] == '-')) i++;
  if (i < n && is_digit(tok[i])) return true;
  return i < n && tok[i] == '.' && n - i >= 2;
}

static Value classify_atom(Reader* r, Ref dst, const char* tok, u32 n, u32 line, u32 col) {
  if (tok_eq(tok, n, "nil")) ot_set_nil(r->vm, dst);
  else if (tok_eq(tok, n, "#t") || tok_eq(tok, n, "#true") || tok_eq(tok, n, "true"))
    ot_set_bool(r->vm, dst, true);
  else if (tok_eq(tok, n, "#f") || tok_eq(tok, n, "#false") || tok_eq(tok, n, "false"))
    ot_set_bool(r->vm, dst, false);
  else if (n > 0 && tok[0] == '#')
    return err(r, line, col, "reserved # syntax");
  else if (n > 0 && tok[0] == ':') {
    if (n == 1) return err(r, line, col, "bare : is not a keyword");
    ot_set_keyword(r->vm, dst, ot_intern(r->vm, tok + 1, n - 1));
  } else if (starts_numerically(tok, n))
    return parse_number(r, dst, tok, n, line, col);
  else
    ot_set_symbol(r->vm, dst, ot_intern(r->vm, tok, n));
  return nil_v();
}

static Value read_atom(Reader* r, Ref dst) {
  u32 line = r->line, col = r->col;
  u32 start = r->pos;
  while (!eof(r) && !atom_end(peek(r))) advance(r);
  return classify_atom(r, dst, r->src + start, r->pos - start, line, col);
}

static Value read_form(Reader* r, Ref dst) {
  skip_ws(r);
  if (eof(r)) return need_more(r, r->line, r->col, "unexpected end of input");
  u32 line = r->line, col = r->col;
  char c = peek(r);
  switch (c) {
    case '(': advance(r); return read_list(r, dst, ')', line, col, nullptr);
    case '[': advance(r); return read_list(r, dst, ']', line, col, "array");
    case '{': advance(r); return read_list(r, dst, '}', line, col, "table");
    case ')':
    case ']':
    case '}': return err(r, line, col, "unexpected closing delimiter");
    case '"': advance(r); return read_string(r, dst, line, col);
    case '\'': advance(r); return read_sugar(r, dst, "quote", 5);
    case '`': advance(r); return read_sugar(r, dst, "quasiquote", 10);
    case ',':
      advance(r);
      if (!eof(r) && peek(r) == '@') {
        advance(r);
        return read_sugar(r, dst, "unquote-splicing", 16);
      }
      return read_sugar(r, dst, "unquote", 7);
    default: return read_atom(r, dst);
  }
}

Value reader_next_ref(Reader* r, Ref dst) {
  skip_ws(r);
  if (eof(r)) {
    r->eofFlag = true;
    ot_set_nil(r->vm, dst);
    return nil_v();
  }
  return read_form(r, dst);
}
