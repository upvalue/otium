#include "reader.h"
#include "value.h"
#include "heap.h"
#include "state.h"
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

static void skipWs(Reader* r) {
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

static Value needMore(Reader* r, u32 line, u32 col, const char* what) {
  r->incompleteFlag = true;
  return err(r, line, col, what);
}

static Value readForm(Reader* r);

// Reads elements up to `close`. If ctorSym is non-null the result is a proper
// list whose head is the interned symbol (array/table literals, spec 1.7);
// dotted tails are then a read error inside brackets/braces because a `.`
// token there still follows the dotted-pair rule of the plain-list grammar.
static Value readList(Reader* r, char close, u32 openLine, u32 openCol, const char* ctorSym) {
  State* vm = r->vm;
  u32 sc = scope_begin(vm);
  u32 base = sc;
  u32 count = 0;
  bool haveTail = false;
  Slot tail = {vm, 0};

  for (;;) {
    skipWs(r);
    if (eof(r)) return scope_exit(vm, sc, needMore(r, openLine, openCol, "unterminated list"));
    char c = peek(r);
    if (c == close) {
      advance(r);
      break;
    }
    if (c == ')' || c == ']' || c == '}')
      return scope_exit(vm, sc, err(r, r->line, r->col, "mismatched closing delimiter"));
    // dotted-tail marker: a lone `.` followed by whitespace/delimiter/eof
    if (c == '.' && (r->pos + 1 >= r->len || atom_end(r->src[r->pos + 1]))) {
      u32 dl = r->line, dc = r->col;
      advance(r);
      if (count == 0)
        return scope_exit(vm, sc, err(r, dl, dc, "dotted tail with no preceding element"));
      if (close != ')')
        return scope_exit(vm, sc, err(r, dl, dc, "dotted tail not allowed in collection literal"));
      Value t = readForm(r);
      OT_TRYS(vm, sc, t);
      tail = scope_push(vm, t);
      haveTail = true;
      skipWs(r);
      if (eof(r))
        return scope_exit(vm, sc, needMore(r, r->line, r->col, "expected ) after dotted tail"));
      if (peek(r) != close)
        return scope_exit(vm, sc, err(r, r->line, r->col, "expected ) after dotted tail"));
      advance(r);
      break;
    }
    Value v = readForm(r);
    OT_TRYS(vm, sc, v);
    state_push(vm, v);  // elements stay contiguous at base..base+count
    count++;
  }

  // Fold right-to-left into a chain of pairs, keeping the accumulator rooted.
  Slot acc = scope_push(vm, haveTail ? slot_get(tail) : null_v());
  for (u32 i = count; i > 0; i--)
    slot_set(acc, make_pair(vm, vm->stack.data[base + i - 1], slot_get(acc)));
  if (ctorSym) {
    Value head = symbol_v(intern_id(&vm->intern, ctorSym, (u32)strlen(ctorSym)));
    slot_set(acc, make_pair(vm, head, slot_get(acc)));
  }
  Value result = slot_get(acc);
  return scope_exit(vm, sc, result);
}

static Value readString(Reader* r, u32 openLine, u32 openCol) {
  Buf out = {0};
  for (;;) {
    if (eof(r)) {
      buf_deinit(&out);
      return needMore(r, openLine, openCol, "unterminated string");
    }
    char c = peek(r);
    advance(r);
    if (c == '"') break;
    if (c == '\\') {
      if (eof(r)) {
        buf_deinit(&out);
        return needMore(r, openLine, openCol, "unterminated string");
      }
      char e = peek(r);
      u32 el = r->line, ec = r->col;
      advance(r);
      switch (e) {
        case 'n': vec_push(&out, '\n'); break;
        case 't': vec_push(&out, '\t'); break;
        case 'r': vec_push(&out, '\r'); break;
        case '0': vec_push(&out, '\0'); break;
        case 'e': vec_push(&out, '\x1b'); break;
        case '"': vec_push(&out, '"'); break;
        case '\\': vec_push(&out, '\\'); break;
        default: buf_deinit(&out); return err(r, el, ec, "unknown string escape");
      }
      continue;
    }
    vec_push(&out, c);
  }
  Value s = make_string_buf(r->vm, &out);
  buf_deinit(&out);
  return s;
}

static Value readSugar(Reader* r, const char* sym, u32 symLen) {
  State* vm = r->vm;
  Value inner = readForm(r);
  OT_TRY(inner);
  u32 sc = scope_begin(vm);
  Slot slot = scope_push(vm, inner);
  slot_set(slot, make_pair(vm, slot_get(slot), null_v()));
  Value head = symbol_v(intern_id(&vm->intern, sym, symLen));
  return scope_exit(vm, sc, make_pair(vm, head, slot_get(slot)));
}

static Value parseNumber(Reader* r, const char* tok, u32 n, u32 line, u32 col) {
  u32 i = 0;
  bool neg = false;
  if (tok[i] == '+' || tok[i] == '-') {
    neg = tok[i] == '-';
    i++;
  }

  // hexadecimal: 0x...
  if (n - i > 2 && tok[i] == '0' && (tok[i + 1] == 'x' || tok[i + 1] == 'X')) {
    u64 acc = 0;
    for (u32 j = i + 2; j < n; j++) {
      int h = hex_val(tok[j]);
      if (h < 0) return err(r, line, col, "invalid hexadecimal literal");
      if (acc > (u64)0x0FFFFFFFFFFFFFFFull)  // acc*16 would overflow u64
        return err(r, line, col, "integer literal out of range");
      acc = acc * 16 + (u64)h;
    }
    u64 limit = neg ? (u64)1 << 63 : ((u64)1 << 63) - 1;
    if (acc > limit) return err(r, line, col, "integer literal out of range");
    i64 value = neg ? (acc == ((u64)1 << 63) ? INT64_MIN : -(i64)acc) : (i64)acc;
    return int_v(value);
  }

  // classify int vs float by presence of . e E
  bool isFloat = false;
  for (u32 j = i; j < n; j++) {
    char c = tok[j];
    if (c == '.' || c == 'e' || c == 'E') {
      isFloat = true;
      break;
    }
  }

  if (!isFloat) {
    // Hand-rolled (never a libc call), so this stays here rather than behind
    // the numio seam; num_parse_i64 extracts the same accumulation for hosts.
    u64 acc = 0;
    for (u32 j = i; j < n; j++) {
      if (!is_digit(tok[j])) return err(r, line, col, "invalid number");
      u32 d = (u32)(tok[j] - '0');
      if (acc > ((u64)0xFFFFFFFFFFFFFFFFull - d) / 10)
        return err(r, line, col, "integer literal out of range");
      acc = acc * 10 + d;
    }
    u64 limit = neg ? (u64)1 << 63 : ((u64)1 << 63) - 1;
    if (acc > limit) return err(r, line, col, "integer literal out of range");
    i64 value = neg ? (acc == ((u64)1 << 63) ? INT64_MIN : -(i64)acc) : (i64)acc;
    return int_v(value);
  }

  // float: numio NUL-terminates a copy, parses with strtod, requires full
  // consumption; its 64-byte scratch bound is surfaced as a distinct error.
  if (n >= 64) return err(r, line, col, "number literal too long");
  f64 f = 0;
  if (!num_parse_f64(tok, n, &f)) return err(r, line, col, "invalid number");
  return float_v(f);
}

static bool tok_eq(const char* tok, u32 n, const char* lit) {
  u32 l = (u32)strlen(lit);
  return n == l && memcmp(tok, lit, n) == 0;
}

static bool starts_numerically(const char* tok, u32 n) {
  u32 i = 0;
  if (i < n && (tok[i] == '+' || tok[i] == '-')) i++;
  if (i < n && is_digit(tok[i])) return true;
  if (i < n && tok[i] == '.' && n - i >= 2) return true;
  return false;
}

static Value classifyAtom(Reader* r, const char* tok, u32 n, u32 line, u32 col) {
  // Spec 1.3 classification order.
  if (tok_eq(tok, n, "nil")) return nil_v();
  if (tok_eq(tok, n, "#t") || tok_eq(tok, n, "#true") || tok_eq(tok, n, "true"))
    return bool_v(true);
  if (tok_eq(tok, n, "#f") || tok_eq(tok, n, "#false") || tok_eq(tok, n, "false"))
    return bool_v(false);
  if (n > 0 && tok[0] == '#') return err(r, line, col, "reserved # syntax");
  if (n > 0 && tok[0] == ':') {
    if (n == 1) return err(r, line, col, "bare : is not a keyword");
    return keyword_v(intern_id(&r->vm->intern, tok + 1, n - 1));
  }
  if (starts_numerically(tok, n)) return parseNumber(r, tok, n, line, col);
  return symbol_v(intern_id(&r->vm->intern, tok, n));
}

static Value readAtom(Reader* r) {
  u32 line = r->line, col = r->col;
  u32 start = r->pos;
  while (!eof(r) && !atom_end(peek(r))) advance(r);
  return classifyAtom(r, r->src + start, r->pos - start, line, col);
}

static Value readForm(Reader* r) {
  skipWs(r);
  if (eof(r)) return needMore(r, r->line, r->col, "unexpected end of input");
  u32 line = r->line, col = r->col;
  char c = peek(r);
  switch (c) {
    case '(': advance(r); return readList(r, ')', line, col, nullptr);
    case '[': advance(r); return readList(r, ']', line, col, "array");
    case '{': advance(r); return readList(r, '}', line, col, "table");
    case ')':
    case ']':
    case '}': return err(r, line, col, "unexpected closing delimiter");
    case '"': advance(r); return readString(r, line, col);
    case '\'': advance(r); return readSugar(r, "quote", 5);
    case '`': advance(r); return readSugar(r, "quasiquote", 10);
    case ',':
      advance(r);
      if (!eof(r) && peek(r) == '@') {
        advance(r);
        return readSugar(r, "unquote-splicing", 16);
      }
      return readSugar(r, "unquote", 7);
    default: return readAtom(r);
  }
}

Value reader_next(Reader* r) {
  skipWs(r);
  if (eof(r)) {
    r->eofFlag = true;
    return nil_v();
  }
  return readForm(r);
}
