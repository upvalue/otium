// tcl.h - a minimal dependency C++ Tcl interpreter
// Uses only malloc/free and printf from standard library

#ifndef _TCL_H
#define _TCL_H

#include "otcommon.h"
#include "otu/shell.h"

// Placement new operator (avoids needing <new>)
inline void *operator new(size_t, void *p) noexcept { return p; }
inline void operator delete(void *, void *) noexcept {}

namespace tcl {

// Memory allocation helpers using malloc/free
template <typename T, typename... Args> T *tcl_new(Args &&...args) {
  void *mem = malloc(sizeof(T));
  if (!mem)
    return nullptr;
  return new (mem) T(static_cast<Args &&>(args)...);
}

template <typename T> void tcl_delete(T *ptr) {
  if (ptr) {
    ptr->~T();
    free(ptr);
  }
}

// Forward declarations
class string;
struct string_view;
template <typename T> class vector;

/**
 * Lightweight string class using malloc/free
 */
class string {
private:
  char *data_;
  size_t len_;
  size_t cap_;

  void ensure_capacity(size_t new_cap);

public:
  string();
  string(const char *s);
  string(const char *s, size_t n);
  string(const string &other);
  string(string &&other) noexcept;
  ~string();

  string &operator=(const string &other);
  string &operator=(string &&other) noexcept;
  string &operator=(const char *s);

  size_t length() const { return len_; }
  size_t size() const { return len_; }
  size_t capacity() const { return cap_; }
  const char *c_str() const { return data_ ? data_ : ""; }
  const char *data() const { return data_ ? data_ : ""; }
  bool empty() const { return len_ == 0; }

  char operator[](size_t i) const { return data_[i]; }
  char &operator[](size_t i) { return data_[i]; }
  char at(size_t i) const { return data_[i]; }

  void clear();
  void reserve(size_t new_cap);
  void append(const char *s, size_t n);
  void append(const char *s);
  void append(const string &s);
  void push_back(char c);

  string &operator+=(const char *s);
  string &operator+=(const string &s);
  string &operator+=(char c);

  int compare(const char *s) const;
  int compare(const string &s) const;
  int compare(const string_view &s) const;

  string substr(size_t pos, size_t len) const;
};

/**
 * Lightweight string view (non-owning reference)
 */
struct string_view {
  const char *data_;
  size_t len_;

  string_view() : data_(nullptr), len_(0) {}
  string_view(const char *s) : data_(s), len_(s ? strlen(s) : 0) {}
  string_view(const char *s, size_t n) : data_(s), len_(n) {}
  string_view(const string &s) : data_(s.c_str()), len_(s.length()) {}

  const char *data() const { return data_ ? data_ : ""; }
  size_t length() const { return len_; }
  size_t size() const { return len_; }
  bool empty() const { return len_ == 0; }

  char operator[](size_t i) const { return data_[i]; }

  string_view substr(size_t pos, size_t len) const {
    if (pos > len_)
      pos = len_;
    if (pos + len > len_)
      len = len_ - pos;
    return string_view(data_ + pos, len);
  }

  int compare(const char *s) const {
    size_t slen = strlen(s);
    int cmp = memcmp(data_, s, len_ < slen ? len_ : slen);
    if (cmp == 0)
      return len_ < slen ? -1 : (len_ > slen ? 1 : 0);
    return cmp;
  }

  int compare(const string_view &other) const {
    size_t minlen = len_ < other.len_ ? len_ : other.len_;
    int cmp = memcmp(data_, other.data_, minlen);
    if (cmp == 0)
      return len_ < other.len_ ? -1 : (len_ > other.len_ ? 1 : 0);
    return cmp;
  }
};

/**
 * Dynamic array template using malloc/free
 */
template <typename T> class vector {
private:
  T *data_;
  size_t size_;
  size_t cap_;

  void ensure_capacity(size_t new_cap) {
    if (new_cap <= cap_)
      return;
    size_t alloc_cap = cap_ == 0 ? 8 : cap_;
    while (alloc_cap < new_cap)
      alloc_cap *= 2;
    T *new_data = (T *)malloc(alloc_cap * sizeof(T));
    if (data_) {
      for (size_t i = 0; i < size_; i++) {
        new (&new_data[i]) T(static_cast<T &&>(data_[i]));
        data_[i].~T();
      }
      free(data_);
    }
    data_ = new_data;
    cap_ = alloc_cap;
  }

public:
  vector() : data_(nullptr), size_(0), cap_(0) {}

  ~vector() {
    if (data_) {
      for (size_t i = 0; i < size_; i++) {
        data_[i].~T();
      }
      free(data_);
    }
  }

  vector(const vector &) = delete;
  vector &operator=(const vector &) = delete;

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  T &operator[](size_t i) { return data_[i]; }
  const T &operator[](size_t i) const { return data_[i]; }

  T &back() { return data_[size_ - 1]; }
  const T &back() const { return data_[size_ - 1]; }

  void push_back(const T &val) {
    ensure_capacity(size_ + 1);
    new (&data_[size_]) T(val);
    size_++;
  }

  void push_back(T &&val) {
    ensure_capacity(size_ + 1);
    new (&data_[size_]) T(static_cast<T &&>(val));
    size_++;
  }

  void pop_back() {
    if (size_ > 0) {
      size_--;
      data_[size_].~T();
    }
  }

  void clear() {
    for (size_t i = 0; i < size_; i++) {
      data_[i].~T();
    }
    size_ = 0;
  }

  // Iterator support (minimal)
  T *begin() { return data_; }
  T *end() { return data_ + size_; }
  const T *begin() const { return data_; }
  const T *end() const { return data_ + size_; }
};

/**
 * Status codes
 */
enum Status { S_OK = 0, S_ERR = 1, S_RETURN = 2, S_BREAK = 3, S_CONTINUE = 4 };

/**
 * Parser token types
 */
enum TokenType {
  TK_ESC = 0,
  TK_STR = 1,
  TK_CMD = 2,
  TK_VAR = 3,
  TK_SEP = 4,
  TK_EOL = 5,
  TK_EOF = 6,
  TK_UNKNOWN = 7,
};

typedef TokenType Token;

// Forward declarations
struct Interp;
struct ProcPrivdata;
struct Cmd;
struct Var;
struct CallFrame;

/**
 * Parser
 */
struct Parser {
  Parser(const string_view &body_, bool trace_parser_ = false);
  ~Parser();

  const string_view body;
  size_t cursor;
  size_t begin, end;
  bool trace_parser;

  bool in_string;
  bool in_brace;
  bool in_quote;
  size_t brace_level;
  Token token;
  char terminating_char;

  bool done();
  char peek();
  char getc();
  void back();
  string_view token_body();
  bool consume_whitespace_check_eol();
  void recurse(Parser &sub, char terminating_char);
  Token _next_token();
  Token next_token();
};

/**
 * ProcPrivdata for Tcl-defined procedures
 */
struct ProcPrivdata {
  ProcPrivdata(string *args_, string *body_);
  ~ProcPrivdata();
  string *args;
  string *body;
};

/**
 * Command function type
 */
typedef Status (*cmd_func_t)(Interp &i, vector<string> &argv,
                             ProcPrivdata *privdata);

/**
 * Command struct
 */
struct Cmd {
  Cmd(const string &name_, cmd_func_t func_, ProcPrivdata *privdata_);
  ~Cmd();
  string name;
  cmd_func_t func;
  ProcPrivdata *privdata;
};

/**
 * Variable
 */
struct Var {
  ~Var();
  string *name, *val;
};

/**
 * Call frame
 */
struct CallFrame {
  ~CallFrame();
  vector<Var *> vars;
};

/**
 * Interpreter
 */
struct Interp {
  vector<Cmd *> commands;
  vector<CallFrame *> callframes;
  string result;
  bool trace_parser;

  Interp();
  ~Interp();

  void drop_call_frame();
  Cmd *get_command(const string &name) const;
  Status register_command(const string &name, cmd_func_t fn,
                          ProcPrivdata *privdata = nullptr);
  Var *get_var(const string_view &name);
  Status set_var(const string &name, const string &val);
  bool arity_check(const string &name, const vector<string> &argv, size_t min,
                   size_t max);
  bool int_check(const string &name, const vector<string> &argv, size_t idx);
  Status eval(const string_view &str);
};

// Global functions
Status call_proc(Interp &i, vector<string> &argv, ProcPrivdata *privdata);
void register_core_commands(Interp &i);

// Helper for printing escaped strings
const char *token_type_str(TokenType t);

// String formatting helper (replaces ostringstream)
void format_error(string &result, const char *fmt, ...);

} // namespace tcl

#endif
