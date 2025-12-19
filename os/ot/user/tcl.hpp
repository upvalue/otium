// tcl.h - a minimal dependency C++ Tcl interpreter
// Uses only malloc/free and printf from standard library

#ifndef _TCL_H
#define _TCL_H

#include "ot/common.h"
#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/user/hashmap.hpp"
#include "ot/user/string.hpp"
#include "ot/user/vector.hpp"

namespace tcl {

// Import ou:: types into tcl:: namespace for convenience
using ou::ou_delete;
using ou::ou_new;
using ou::string;
using ou::string_view;
using ou::vector;
template <typename V> using HashMap = ou::StringHashMap<V>;

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
 * I/O backend interface for interpreter output.
 * Override to redirect puts, help, etc. output to custom destinations.
 * Default (nullptr) writes to console via oprintf.
 */
struct TclIO {
  virtual void write(const char *str) = 0;
  virtual void write_error(const char *str) = 0;
  virtual ~TclIO() = default;
};

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
  bool has_escapes_;
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
  bool has_escapes() const;
};

/**
 * Base class for command private data.
 * Can be subclassed to pass custom data to command functions.
 */
struct ProcPrivdata {
  ProcPrivdata() : args(nullptr), body(nullptr) {}
  ProcPrivdata(string *args_, string *body_);
  virtual ~ProcPrivdata();
  string *args;
  string *body;
};

/**
 * Command function type
 */
typedef Status (*cmd_func_t)(Interp &i, vector<string> &argv, ProcPrivdata *privdata);

/**
 * Command struct
 */
struct Cmd {
  Cmd(const string &name_, cmd_func_t func_, ProcPrivdata *privdata_ = nullptr, const string &docstring_ = "");
  ~Cmd();
  string name;
  cmd_func_t func;
  ProcPrivdata *privdata;
  string docstring;
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
  HashMap<Cmd *> cmd_hash_;
  vector<CallFrame *> callframes;
  string result;
  bool trace_parser;

  // MessagePack support (optional)
  char *mpack_buffer_;
  size_t mpack_buffer_size_;
  MPackWriter mpack_writer_;

  // I/O backend (nullptr = default console output)
  TclIO *io_;

  Interp();
  ~Interp();

  void drop_call_frame();
  Cmd *get_command(const string &name) const;
  Status register_command(const string &name, cmd_func_t fn, ProcPrivdata *privdata = nullptr,
                          const string &docstring = "");
  Var *get_var(const string_view &name);
  Status set_var(const string &name, const string &val);
  bool arity_check(const string &name, const vector<string> &argv, size_t min, size_t max);
  bool int_check(const string &name, const vector<string> &argv, size_t idx);
  Status eval(const string_view &str);

  // I/O methods - use configured backend or default to console
  void set_io(TclIO *io);
  void write(const char *str);
  void write_error(const char *str);

  // MessagePack functions
  void register_mpack_functions(char *buffer, size_t size);
};

// Global functions
Status call_proc(Interp &i, vector<string> &argv, ProcPrivdata *privdata);
void register_core_commands(Interp &i);

// Helper for printing escaped strings
const char *token_type_str(TokenType t);

// String formatting helper (replaces ostringstream)
void format_error(string &result, const char *fmt, ...);

// List helper functions
void list_parse(const string_view &list_str, vector<string> &elements);
void list_format(const vector<string> &elements, string &result);

// Inline helper functions for performance

// Validate and parse integer in one pass (eliminates duplicate atoi calls)
inline BoolResult<int> parse_and_check_int(const string &str) {
  if (str.empty()) {
    return BoolResult<int>::err(false);
  }

  size_t i = 0;
  bool negative = false;

  // Handle sign
  if (str[0] == '-') {
    negative = true;
    i++;
  } else if (str[0] == '+') {
    i++;
  }

  // Need at least one digit after sign
  if (i >= str.length()) {
    return BoolResult<int>::err(false);
  }

  // Parse digits and validate
  int result = 0;
  for (; i < str.length(); i++) {
    char c = str[i];
    if (c < '0' || c > '9') {
      return BoolResult<int>::err(false);
    }
    result = result * 10 + (c - '0');
  }

  return BoolResult<int>::ok(negative ? -result : result);
}

// Arity check - inlined for performance
inline bool Interp::arity_check(const string &name, const vector<string> &argv, size_t min, size_t max) {
  if (min == max && argv.size() != min) {
    format_error(result, "wrong number of args for %s (expected %zu)", name.c_str(), min);
    return false;
  }
  if (argv.size() < min || argv.size() > max) {
    format_error(result, "[%s]: wrong number of args (expected %zu to %zu)", name.c_str(), min, max);
    return false;
  }
  return true;
}

} // namespace tcl

#endif
