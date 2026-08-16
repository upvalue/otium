#include "reader.hpp"
#include "value.hpp"
#include "heap.hpp"
#include "vm.hpp"
#include <cstring>
#include <cstdlib>  // strtod (deviation from allowed-header list; noted)

namespace ot {

static bool is_ws(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

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

static bool atom_end(char c) { return is_ws(c) || is_delim(c); }

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

Reader::Reader(Vm& vm, const char* src, u32 len, const char* filename)
    : vm_(vm), src_(src), len_(len), filename_(filename) {}

void Reader::advance() {
  if (pos_ >= len_) return;
  if (src_[pos_] == '\n') {
    line_++;
    col_ = 1;
  } else {
    col_++;
  }
  pos_++;
}

void Reader::skipWs() {
  while (!eof()) {
    char c = peek();
    if (is_ws(c)) {
      advance();
      continue;
    }
    if (c == ';') {
      while (!eof() && peek() != '\n') advance();
      continue;
    }
    break;
  }
}

Value Reader::err(u32 line, u32 col, const char* what) {
  return raise_error(vm_, "read error at %u:%u: %s", line, col, what);
}

Value Reader::next() {
  skipWs();
  if (eof()) {
    eof_ = true;
    return nil_v();
  }
  return readForm();
}

Value Reader::readForm() {
  skipWs();
  if (eof()) return err(line_, col_, "unexpected end of input");
  u32 line = line_, col = col_;
  char c = peek();
  switch (c) {
    case '(': advance(); return readList(')', line, col, nullptr);
    case '[': advance(); return readList(']', line, col, "array");
    case '{': advance(); return readList('}', line, col, "table");
    case ')':
    case ']':
    case '}': return err(line, col, "unexpected closing delimiter");
    case '"': advance(); return readString(line, col);
    case '\'': advance(); return readSugar("quote", 5);
    case '`': advance(); return readSugar("quasiquote", 10);
    case ',':
      advance();
      if (!eof() && peek() == '@') {
        advance();
        return readSugar("unquote-splicing", 16);
      }
      return readSugar("unquote", 7);
    default: return readAtom();
  }
}

// Reads elements up to `close`. If ctorSym is non-null the result is a proper
// list whose head is the interned symbol (array/table literals, spec 1.7);
// dotted tails are then a read error inside brackets/braces because a `.`
// token there still follows the dotted-pair rule of the plain-list grammar.
Value Reader::readList(char close, u32 openLine, u32 openCol, const char* ctorSym) {
  Scope sc(vm_);
  u32 base = sc.base;
  u32 count = 0;
  bool haveTail = false;
  Slot tail{&vm_, 0};

  for (;;) {
    skipWs();
    if (eof()) return err(openLine, openCol, "unterminated list");
    char c = peek();
    if (c == close) {
      advance();
      break;
    }
    if (c == ')' || c == ']' || c == '}')
      return err(line_, col_, "mismatched closing delimiter");
    // dotted-tail marker: a lone `.` followed by whitespace/delimiter/eof
    if (c == '.' && (pos_ + 1 >= len_ || atom_end(src_[pos_ + 1]))) {
      u32 dl = line_, dc = col_;
      advance();
      if (count == 0) return err(dl, dc, "dotted tail with no preceding element");
      if (close != ')') return err(dl, dc, "dotted tail not allowed in collection literal");
      Value t = readForm();
      if (t.tag == Tag::Unwind) return t;
      tail = sc.push(t);
      haveTail = true;
      skipWs();
      if (eof() || peek() != close) return err(line_, col_, "expected ) after dotted tail");
      advance();
      break;
    }
    Value v = readForm();
    if (v.tag == Tag::Unwind) return v;
    vm_.push(v);  // elements stay contiguous at base..base+count
    count++;
  }

  // Fold right-to-left into a chain of pairs, keeping the accumulator rooted.
  Slot acc = sc.push(haveTail ? tail.get() : null_v());
  for (u32 i = count; i > 0; i--) acc.set(make_pair(vm_, vm_.stack[base + i - 1], acc.get()));
  if (ctorSym) {
    Value head = symbol_v(vm_.intern.intern(ctorSym, (u32)strlen(ctorSym)));
    acc.set(make_pair(vm_, head, acc.get()));
  }
  Value result = acc.get();
  return result;
}

Value Reader::readString(u32 openLine, u32 openCol) {
  Buf out;
  for (;;) {
    if (eof()) return err(openLine, openCol, "unterminated string");
    char c = peek();
    advance();
    if (c == '"') break;
    if (c == '\\') {
      if (eof()) return err(openLine, openCol, "unterminated string");
      char e = peek();
      u32 el = line_, ec = col_;
      advance();
      switch (e) {
        case 'n': out.push('\n'); break;
        case 't': out.push('\t'); break;
        case 'r': out.push('\r'); break;
        case '0': out.push('\0'); break;
        case 'e': out.push('\x1b'); break;
        case '"': out.push('"'); break;
        case '\\': out.push('\\'); break;
        default: return err(el, ec, "unknown string escape");
      }
      continue;
    }
    out.push(c);
  }
  return make_string(vm_, out.data ? out.data : "", out.len);
}

Value Reader::readSugar(const char* sym, u32 symLen) {
  Value inner = readForm();
  if (inner.tag == Tag::Unwind) return inner;
  Scope sc(vm_);
  Slot slot = sc.push(inner);
  slot.set(make_pair(vm_, slot.get(), null_v()));
  Value head = symbol_v(vm_.intern.intern(sym, symLen));
  return make_pair(vm_, head, slot.get());
}

Value Reader::readAtom() {
  u32 line = line_, col = col_;
  u32 start = pos_;
  while (!eof() && !atom_end(peek())) advance();
  return classifyAtom(src_ + start, pos_ - start, line, col);
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

Value Reader::classifyAtom(const char* tok, u32 n, u32 line, u32 col) {
  // Spec 1.3 classification order.
  if (tok_eq(tok, n, "nil")) return nil_v();
  if (tok_eq(tok, n, "#t") || tok_eq(tok, n, "#true") || tok_eq(tok, n, "true"))
    return bool_v(true);
  if (tok_eq(tok, n, "#f") || tok_eq(tok, n, "#false") || tok_eq(tok, n, "false"))
    return bool_v(false);
  if (n > 0 && tok[0] == '#') return err(line, col, "reserved # syntax");
  if (n > 0 && tok[0] == ':') {
    if (n == 1) return err(line, col, "bare : is not a keyword");
    return keyword_v(vm_.intern.intern(tok + 1, n - 1));
  }
  if (starts_numerically(tok, n)) return parseNumber(tok, n, line, col);
  return symbol_v(vm_.intern.intern(tok, n));
}

Value Reader::parseNumber(const char* tok, u32 n, u32 line, u32 col) {
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
      if (h < 0) return err(line, col, "invalid hexadecimal literal");
      if (acc > (u64)0x0FFFFFFFFFFFFFFFull)  // acc*16 would overflow u64
        return err(line, col, "integer literal out of range");
      acc = acc * 16 + (u64)h;
    }
    u64 limit = neg ? (u64)1 << 63 : ((u64)1 << 63) - 1;
    if (acc > limit) return err(line, col, "integer literal out of range");
    return int_v(neg ? -(i64)acc : (i64)acc);
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
    u64 acc = 0;
    for (u32 j = i; j < n; j++) {
      if (!is_digit(tok[j])) return err(line, col, "invalid number");
      u32 d = (u32)(tok[j] - '0');
      if (acc > ((u64)0xFFFFFFFFFFFFFFFFull - d) / 10)
        return err(line, col, "integer literal out of range");
      acc = acc * 10 + d;
    }
    u64 limit = neg ? (u64)1 << 63 : ((u64)1 << 63) - 1;
    if (acc > limit) return err(line, col, "integer literal out of range");
    return int_v(neg ? -(i64)acc : (i64)acc);
  }

  // float: NUL-terminate a copy, parse with strtod, require full consumption
  char buf[64];
  if (n >= sizeof(buf)) return err(line, col, "number literal too long");
  memcpy(buf, tok, n);
  buf[n] = '\0';
  char* end = nullptr;
  f64 f = strtod(buf, &end);
  if (end != buf + n) return err(line, col, "invalid number");
  return float_v(f);
}

}  // namespace ot
