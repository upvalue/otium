// tcl.cpp - implementation of minimal dependency Tcl interpreter

#include <stdio.h>

#include "ot/lib/mpack/mpack-utils.hpp"
#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/user/tcl.hpp"

namespace tcl {

//
// HELPER FUNCTIONS
//

const char *token_type_str(TokenType t) {
  switch (t) {
  case TK_ESC:
    return "TK_ESC";
  case TK_STR:
    return "TK_STR";
  case TK_CMD:
    return "TK_CMD";
  case TK_VAR:
    return "TK_VAR";
  case TK_SEP:
    return "TK_SEP";
  case TK_EOL:
    return "TK_EOL";
  case TK_EOF:
    return "TK_EOF";
  case TK_UNKNOWN:
    return "TK_UNKNOWN";
  }
  return "UNKNOWN";
}

void format_error(string &result, const char *fmt, ...) {
  char buffer[4096];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  result = buffer;
}

static string process_escapes(const string_view &input) {
  string result;
  for (size_t i = 0; i < input.length(); i++) {
    if (input[i] == '\\' && i + 1 < input.length()) {
      char next = input[i + 1];
      switch (next) {
      case '"':
        result += '"';
        i++;
        break;
      case '\\':
        result += '\\';
        i++;
        break;
      case 'n':
        result += '\n';
        i++;
        break;
      case 't':
        result += '\t';
        i++;
        break;
      case 'r':
        result += '\r';
        i++;
        break;
      default:
        // Unknown escape: pass through literally
        result += input[i];
        break;
      }
    } else {
      result += input[i];
    }
  }
  return result;
}

//
// PARSER IMPLEMENTATION
//

Parser::Parser(const string_view &body_, bool trace_parser_)
    : body(body_), cursor(0), begin(0), end(0), trace_parser(trace_parser_), in_string(false), in_brace(false),
      in_quote(false), has_escapes_(false), brace_level(0), token(TK_EOL), terminating_char(0) {}

bool Parser::has_escapes() const { return has_escapes_; }

Parser::~Parser() {}

bool Parser::done() { return cursor >= body.size(); }

char Parser::peek() { return body[cursor]; }

char Parser::getc() { return body[cursor++]; }

void Parser::back() { cursor--; }

string_view Parser::token_body() { return body.substr(begin, end - begin); }

bool Parser::consume_whitespace_check_eol() {
  while (!done()) {
    char c = peek();
    if (c == '\n') {
      return true;
    } else if (c == ' ' || c == '\r' || c == '\t' || c == ';') {
      getc();
    } else {
      break;
    }
  }
  return false;
}

void Parser::recurse(Parser &sub, char terminating_char) {
  sub.terminating_char = terminating_char;
  while (true) {
    Token tk = sub.next_token();
    if (tk == TK_EOF) {
      break;
    }
  }
  cursor = cursor + sub.cursor;
}

Token Parser::_next_token() {
  int adj = 0;
  has_escapes_ = false;
start:
  if (done()) {
    if (token != TK_EOL && token != TK_EOF) {
      token = TK_EOL;
    } else {
      token = TK_EOF;
    }
    return token;
  }

  token = TK_ESC;
  begin = cursor;
  while (!done()) {
    adj = 0;
    char c = getc();

    if (terminating_char && c == terminating_char) {
      end = cursor;
      return TK_EOF;
    }

    switch (c) {
    case '{': {
      if (in_quote || in_string)
        continue;
      if (!in_brace) {
        begin++;
        token = TK_STR;
        in_brace = true;
      }
      brace_level++;
      break;
    }
    case '}': {
      if (in_quote || in_string)
        continue;
      if (brace_level > 0) {
        brace_level--;
        if (brace_level == 0) {
          in_brace = false;
          adj = 1;
          goto finish;
        }
        break;
      }
    }
    case '[': {
      if (in_quote || in_string || in_brace)
        continue;
      begin++;
      Parser sub(body.substr(cursor, body.length() - cursor));
      recurse(sub, ']');
      adj = 1;
      token = TK_CMD;
      goto finish;
    }
    case '$': {
      if (in_string || in_brace)
        continue;
      if (in_quote && cursor != begin + 1) {
        back();
        goto finish;
      }
      begin++;
      token = TK_VAR;
      in_string = true;
      break;
    }
    case '#': {
      if (in_string || in_quote || in_brace)
        continue;
      while (!done()) {
        if (getc() == '\n')
          break;
      }
      goto start;
    }
    case '\\': {
      if (in_quote && !done()) {
        char next = peek();
        if (next == '"' || next == '\\' || next == 'n' || next == 't' || next == 'r') {
          getc(); // consume escaped char so \" doesn't end the string
          has_escapes_ = true;
        }
        // For unknown escapes, backslash passes through
      }
      continue;
    }
    case '\"': {
      if (in_brace)
        continue; // quotes inside braces are literal
      if (in_quote) {
        in_quote = false;
        adj = 1;
        goto finish;
      }
      in_quote = true;
      begin++;
      adj = 1;
      continue;
    }
    case ' ':
    case '\n':
    case '\r':
    case '\t':
    case ';':
      if (in_brace) {
        continue;
      }
      if (in_string) {
        back();
        in_string = false;
        goto finish;
      }
      if (in_quote) {
        continue;
      }
      token = (c == '\n' || c == ';') ? TK_EOL : TK_SEP;
      if (consume_whitespace_check_eol()) {
        token = TK_EOL;
      }
      goto finish;
    default: {
      if (!in_quote && !in_brace) {
        in_string = true;
      }
    }
    }
  }
finish:
  end = cursor - adj;
  return token;
}

Token Parser::next_token() {
  Token t = _next_token();
  return t;
}

//
// PRIVDATA IMPLEMENTATIONS
//

ProcPrivdata::ProcPrivdata(string *args_, string *body_) : args(args_), body(body_) {}

ProcPrivdata::~ProcPrivdata() {
  ou_delete(args);
  ou_delete(body);
}

//
// CMD IMPLEMENTATION
//

Cmd::Cmd(const string &name_, cmd_func_t func_, ProcPrivdata *privdata_, const string &docstring_)
    : name(name_), func(func_), privdata(privdata_), docstring(docstring_) {}

Cmd::~Cmd() {
  if (privdata)
    ou_delete(privdata);
}

//
// VAR IMPLEMENTATION
//

Var::~Var() {
  ou_delete(name);
  ou_delete(val);
}

//
// CALLFRAME IMPLEMENTATION
//

CallFrame::~CallFrame() {
  for (Var *v : vars) {
    ou_delete(v);
  }
}

//
// INTERP IMPLEMENTATION
//

Interp::Interp() : trace_parser(false), mpack_buffer_(nullptr), mpack_buffer_size_(0) {
  callframes.push_back(ou_new<CallFrame>());
}

Interp::~Interp() {
  for (CallFrame *cf : callframes) {
    ou_delete(cf);
  }
  for (Cmd *c : commands) {
    ou_delete(c);
  }
}

void Interp::drop_call_frame() {
  CallFrame *cf = callframes.back();
  callframes.pop_back();
  ou_delete(cf);
}

Cmd *Interp::get_command(const string &name) const {
  for (Cmd *c : commands) {
    if (c->name.compare(name) == 0) {
      return c;
    }
  }
  return nullptr;
}

Status Interp::register_command(const string &name, cmd_func_t fn, ProcPrivdata *privdata, const string &docstring) {
  // Check if command already exists
  Cmd *existing = get_command(name);
  if (existing != nullptr) {
    // Reuse existing command, just update fields and free old privdata
    if (existing->privdata) {
      ou_delete(existing->privdata);
    }
    existing->func = fn;
    existing->privdata = privdata;
    existing->docstring = docstring;
    return S_OK;
  }
  // Register new command
  commands.push_back(ou_new<Cmd>(name, fn, privdata, docstring));
  return S_OK;
}

Var *Interp::get_var(const string_view &name) {
  for (Var *v : callframes.back()->vars) {
    if (v->name->compare(name) == 0) {
      return v;
    }
  }
  return nullptr;
}

Status Interp::set_var(const string &name, const string &val) {
  Var *v = get_var(string_view(name));
  if (v) {
    ou_delete(v->val);
    v->val = ou_new<string>(val);
  } else {
    v = ou_new<Var>();
    v->name = ou_new<string>(name);
    v->val = ou_new<string>(val);
    callframes.back()->vars.push_back(v);
  }
  return S_OK;
}

bool Interp::arity_check(const string &name, const vector<string> &argv, size_t min, size_t max) {
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

bool Interp::int_check(const string &name, const vector<string> &argv, size_t idx) {
  const string &arg = argv[idx];
  for (size_t i = 0; i < arg.length(); i++) {
    char c = arg[i];
    if (i == 0 && (c == '-' || c == '+'))
      continue;
    if (c < '0' || c > '9') {
      format_error(result, "[%s]: argument %zu is not an integer", name.c_str(), idx);
      return false;
    }
  }
  return true;
}

Status Interp::eval(const string_view &str) {
  result.clear();
  Parser p(str, trace_parser);
  Status ret = S_OK;
  vector<string> argv;

  while (1) {
    TokenType prevtype = p.token;
    Token token = p.next_token();
    string_view t = p.token_body();

    if (token == TK_EOF) {
      break;
    } else if (token == TK_VAR) {
      Var *v = get_var(t);
      if (v == nullptr) {
        format_error(result, "variable not found: '%.*s'", (int)t.length(), t.data());
        return S_ERR;
      }
      t = string_view(*v->val);
    } else if (token == TK_CMD) {
      ret = eval(t);
      if (ret != S_OK) {
        return ret;
      }
      t = string_view(result);
    } else if (token == TK_SEP) {
      continue;
    } else if (token == TK_EOL) {
      Cmd *c;
      if (argv.size()) {
        if ((c = get_command(argv[0])) == nullptr) {
          format_error(result, "command not found: '%s'", argv[0].c_str());
          return S_ERR;
        }
        Status s = c->func(*this, argv, c->privdata);
        if (s != S_OK) {
          return s;
        }
      }
      argv.clear();
      continue;
    }

    string token_str = p.has_escapes() ? process_escapes(t) : string(t.data(), t.length());
    if (prevtype == TK_SEP || prevtype == TK_EOL) {
      argv.push_back(token_str);
    } else {
      argv[argv.size() - 1] += token_str;
    }
    prevtype = token;
  }
  return S_OK;
}

//
// CALL_PROC IMPLEMENTATION
//

Status call_proc(Interp &i, vector<string> &argv, ProcPrivdata *pd) {
  CallFrame *cf = ou_new<CallFrame>();
  i.callframes.push_back(cf);

  size_t arity = 0;
  string *alist = pd->args;
  string *body = pd->body;

  // Parse arguments list
  size_t j = 0, start = 0;
  for (; j < alist->size(); j++) {
    while (j < alist->size() && alist->at(j) == ' ') {
      j++;
    }
    start = j;
    while (j < alist->size() && alist->at(j) != ' ') {
      j++;
    }
    // Got argument
    i.set_var(alist->substr(start, j - start), argv[arity + 1]);
    arity++;
    if (j >= alist->size())
      break;
  }

  Status s = S_OK;
  if (arity != argv.size() - 1) {
    format_error(i.result, "wrong number of arguments for %s got %zu expected %zu", argv[0].c_str(), argv.size(),
                 arity);
    s = S_ERR;
  } else {
    s = i.eval(string_view(*body));
    if (s == S_RETURN) {
      s = S_OK;
    }
  }

  i.drop_call_frame();
  return s;
}

//
// STDLIB COMMANDS
//

static Status cmd_puts(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("puts", argv, 2, 2)) {
    return S_ERR;
  }
  oprintf("%s\n", argv[1].c_str());
  return S_OK;
}

static Status cmd_set(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("set", argv, 3, 3)) {
    return S_ERR;
  }
  i.set_var(argv[1], argv[2]);
  return S_OK;
}

static Status cmd_if(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("if", argv, 3, 5)) {
    return S_ERR;
  }
  if (i.eval(string_view(argv[1])) != S_OK) {
    return S_ERR;
  }
  if (atoi(i.result.c_str())) {
    return i.eval(string_view(argv[2]));
  } else if (argv.size() == 5) {
    return i.eval(string_view(argv[4]));
  }
  return S_OK;
}

static Status cmd_while(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("while", argv, 3, 3)) {
    return S_ERR;
  }
  while (1) {
    Status s = i.eval(string_view(argv[1]));
    if (s != S_OK) {
      return s;
    }
    if (atoi(i.result.c_str())) {
      s = i.eval(string_view(argv[2]));
      if (s == S_CONTINUE || s == S_OK) {
        continue;
      } else if (s == S_BREAK) {
        return S_OK;
      } else {
        return s;
      }
    } else {
      return S_OK;
    }
  }
  return S_OK;
}

static Status cmd_break(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("break", argv, 1, 1)) {
    return S_ERR;
  }
  return S_BREAK;
}

static Status cmd_continue(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("continue", argv, 1, 1)) {
    return S_ERR;
  }
  return S_CONTINUE;
}

static Status cmd_proc(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("proc", argv, 4, 4)) {
    return S_ERR;
  }
  ProcPrivdata *ppd = ou_new<ProcPrivdata>(ou_new<string>(argv[2]), ou_new<string>(argv[3]));
  i.register_command(argv[1], call_proc, ppd);
  return S_OK;
}

static Status cmd_return(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("return", argv, 1, 2)) {
    return S_ERR;
  }
  if (argv.size() == 2) {
    i.result = argv[1];
  }
  return S_RETURN;
}

static Status cmd_add(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("+", argv, 3, 3)) {
    return S_ERR;
  }
  if (!i.int_check("+", argv, 1) || !i.int_check("+", argv, 2)) {
    return S_ERR;
  }
  int result = atoi(argv[1].c_str()) + atoi(argv[2].c_str());
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", result);
  i.result = buf;
  return S_OK;
}

static Status cmd_sub(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("-", argv, 3, 3)) {
    return S_ERR;
  }
  if (!i.int_check("-", argv, 1) || !i.int_check("-", argv, 2)) {
    return S_ERR;
  }
  int result = atoi(argv[1].c_str()) - atoi(argv[2].c_str());
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", result);
  i.result = buf;
  return S_OK;
}

static Status cmd_mul(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("*", argv, 3, 3)) {
    return S_ERR;
  }
  if (!i.int_check("*", argv, 1) || !i.int_check("*", argv, 2)) {
    return S_ERR;
  }
  int result = atoi(argv[1].c_str()) * atoi(argv[2].c_str());
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", result);
  i.result = buf;
  return S_OK;
}

static Status cmd_div(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("/", argv, 3, 3)) {
    return S_ERR;
  }
  if (!i.int_check("/", argv, 1) || !i.int_check("/", argv, 2)) {
    return S_ERR;
  }
  int result = atoi(argv[1].c_str()) / atoi(argv[2].c_str());
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", result);
  i.result = buf;
  return S_OK;
}

static Status cmd_eq(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("==", argv, 3, 3)) {
    return S_ERR;
  }
  if (!i.int_check("==", argv, 1) || !i.int_check("==", argv, 2)) {
    return S_ERR;
  }
  int result = atoi(argv[1].c_str()) == atoi(argv[2].c_str());
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", result);
  i.result = buf;
  return S_OK;
}

static Status cmd_ne(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("!=", argv, 3, 3)) {
    return S_ERR;
  }
  if (!i.int_check("!=", argv, 1) || !i.int_check("!=", argv, 2)) {
    return S_ERR;
  }
  int result = atoi(argv[1].c_str()) != atoi(argv[2].c_str());
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", result);
  i.result = buf;
  return S_OK;
}

static Status cmd_gt(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check(">", argv, 3, 3)) {
    return S_ERR;
  }
  if (!i.int_check(">", argv, 1) || !i.int_check(">", argv, 2)) {
    return S_ERR;
  }
  int result = atoi(argv[1].c_str()) > atoi(argv[2].c_str());
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", result);
  i.result = buf;
  return S_OK;
}

static Status cmd_lt(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("<", argv, 3, 3)) {
    return S_ERR;
  }
  if (!i.int_check("<", argv, 1) || !i.int_check("<", argv, 2)) {
    return S_ERR;
  }
  int result = atoi(argv[1].c_str()) < atoi(argv[2].c_str());
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", result);
  i.result = buf;
  return S_OK;
}

static Status cmd_gte(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check(">=", argv, 3, 3)) {
    return S_ERR;
  }
  if (!i.int_check(">=", argv, 1) || !i.int_check(">=", argv, 2)) {
    return S_ERR;
  }
  int result = atoi(argv[1].c_str()) >= atoi(argv[2].c_str());
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", result);
  i.result = buf;
  return S_OK;
}

static Status cmd_lte(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("<=", argv, 3, 3)) {
    return S_ERR;
  }
  if (!i.int_check("<=", argv, 1) || !i.int_check("<=", argv, 2)) {
    return S_ERR;
  }
  int result = atoi(argv[1].c_str()) <= atoi(argv[2].c_str());
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", result);
  i.result = buf;
  return S_OK;
}

static Status cmd_help(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (argv.size() == 1) {
    // List all commands with their docstrings
    oprintf("Available commands:\n");
    for (Cmd *c : i.commands) {
      if (!c->docstring.empty()) {
        oprintf("  %s\n    %s\n", c->name.c_str(), c->docstring.c_str());
      } else {
        oprintf("  %s\n", c->name.c_str());
      }
    }
  } else if (argv.size() == 2) {
    // Show help for specific command
    Cmd *cmd = i.get_command(argv[1]);
    if (cmd) {
      if (!cmd->docstring.empty()) {
        oprintf("%s: %s\n", cmd->name.c_str(), cmd->docstring.c_str());
      } else {
        oprintf("%s: no documentation available\n", cmd->name.c_str());
      }
    } else {
      format_error(i.result, "command not found: '%s'", argv[1].c_str());
      return S_ERR;
    }
  } else {
    format_error(i.result, "[help]: expected 0 or 1 arguments");
    return S_ERR;
  }
  return S_OK;
}

static Status cmd_commands(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("commands", argv, 1, 1)) {
    return S_ERR;
  }
  for (Cmd *c : i.commands) {
    oprintf("%s ", c->name.c_str());
  }
  oprintf("\n");
  return S_OK;
}

//
// LIST HELPER FUNCTIONS
//

// Parse a list string into a vector of elements
static void list_parse(const string_view &list_str, vector<string> &elements) {
  elements.clear();
  size_t i = 0;
  while (i < list_str.length()) {
    // Skip whitespace
    while (i < list_str.length() && (list_str[i] == ' ' || list_str[i] == '\t' || list_str[i] == '\n')) {
      i++;
    }
    if (i >= list_str.length())
      break;

    // Parse element
    if (list_str[i] == '{') {
      // Brace-quoted element
      i++; // Skip opening brace
      size_t start = i;
      int brace_level = 1;
      while (i < list_str.length() && brace_level > 0) {
        if (list_str[i] == '{') {
          brace_level++;
        } else if (list_str[i] == '}') {
          brace_level--;
        }
        if (brace_level > 0)
          i++;
      }
      elements.push_back(string(list_str.data() + start, i - start));
      i++; // Skip closing brace
    } else {
      // Space-separated element
      size_t start = i;
      while (i < list_str.length() && list_str[i] != ' ' && list_str[i] != '\t' && list_str[i] != '\n') {
        i++;
      }
      elements.push_back(string(list_str.data() + start, i - start));
    }
  }
}

// Format a vector of elements as a list string
static void list_format(const vector<string> &elements, string &result) {
  result.clear();
  for (size_t i = 0; i < elements.size(); i++) {
    if (i > 0)
      result += ' ';
    const string &elem = elements[i];
    // Check if element needs braces (empty strings, strings with whitespace, or strings with braces)
    bool needs_braces = (elem.length() == 0);
    if (!needs_braces) {
      for (size_t j = 0; j < elem.length(); j++) {
        if (elem[j] == ' ' || elem[j] == '\t' || elem[j] == '\n' || elem[j] == '{' || elem[j] == '}') {
          needs_braces = true;
          break;
        }
      }
    }
    if (needs_braces) {
      result += '{';
      result += elem;
      result += '}';
    } else {
      result += elem;
    }
  }
}

//
// LIST COMMANDS
//

static Status cmd_list(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  // Create a list from all arguments (argv[1..n])
  vector<string> elements;
  for (size_t j = 1; j < argv.size(); j++) {
    elements.push_back(argv[j]);
  }
  list_format(elements, i.result);
  return S_OK;
}

static Status cmd_lindex(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("lindex", argv, 3, 3)) {
    return S_ERR;
  }
  if (!i.int_check("lindex", argv, 2)) {
    return S_ERR;
  }

  vector<string> elements;
  list_parse(string_view(argv[1]), elements);

  int index = atoi(argv[2].c_str());
  if (index < 0 || (size_t)index >= elements.size()) {
    i.result.clear();
    return S_OK;
  }

  i.result = elements[index];
  return S_OK;
}

static Status cmd_lappend(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("lappend", argv, 2, 1024)) {
    return S_ERR;
  }

  // Get current list from variable
  Var *v = i.get_var(string_view(argv[1]));
  vector<string> elements;
  if (v) {
    list_parse(string_view(*v->val), elements);
  }

  // Append new elements
  for (size_t j = 2; j < argv.size(); j++) {
    elements.push_back(argv[j]);
  }

  // Format and store back
  string new_list;
  list_format(elements, new_list);
  i.set_var(argv[1], new_list);
  i.result = new_list;
  return S_OK;
}

static Status cmd_llength(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("llength", argv, 2, 2)) {
    return S_ERR;
  }

  vector<string> elements;
  list_parse(string_view(argv[1]), elements);

  char buf[32];
  snprintf(buf, sizeof(buf), "%d", (int)elements.size());
  i.result = buf;
  return S_OK;
}

static Status cmd_lrange(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("lrange", argv, 4, 4)) {
    return S_ERR;
  }
  if (!i.int_check("lrange", argv, 2) || !i.int_check("lrange", argv, 3)) {
    return S_ERR;
  }

  vector<string> elements;
  list_parse(string_view(argv[1]), elements);

  int start = atoi(argv[2].c_str());
  int end = atoi(argv[3].c_str());

  if (start < 0)
    start = 0;
  if (end >= (int)elements.size())
    end = elements.size() - 1;

  vector<string> range;
  for (int j = start; j <= end && j < (int)elements.size(); j++) {
    range.push_back(elements[j]);
  }

  list_format(range, i.result);
  return S_OK;
}

static Status cmd_split(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("split", argv, 2, 3)) {
    return S_ERR;
  }

  const string &str = argv[1];
  char delimiter = ' ';
  if (argv.size() == 3) {
    if (argv[2].length() != 1) {
      format_error(i.result, "split: delimiter must be a single character");
      return S_ERR;
    }
    delimiter = argv[2][0];
  }

  vector<string> elements;
  size_t start = 0;
  for (size_t j = 0; j <= str.length(); j++) {
    if (j == str.length() || str[j] == delimiter) {
      elements.push_back(str.substr(start, j - start));
      start = j + 1;
    }
  }

  list_format(elements, i.result);
  return S_OK;
}

static Status cmd_join(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("join", argv, 2, 3)) {
    return S_ERR;
  }

  vector<string> elements;
  list_parse(string_view(argv[1]), elements);

  string separator = " ";
  if (argv.size() == 3) {
    separator = argv[2];
  }

  i.result.clear();
  for (size_t j = 0; j < elements.size(); j++) {
    if (j > 0)
      i.result += separator;
    i.result += elements[j];
  }

  return S_OK;
}

//
// NUMBER CONVERSION COMMANDS
//

static Status cmd_hex(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("hex", argv, 2, 2)) {
    return S_ERR;
  }

  const string &hex_str = argv[1];
  if (hex_str.length() == 0) {
    format_error(i.result, "hex: empty string");
    return S_ERR;
  }

  // Parse hex string (with optional 0x prefix)
  size_t start = 0;
  if (hex_str.length() > 2 && hex_str[0] == '0' && (hex_str[1] == 'x' || hex_str[1] == 'X')) {
    start = 2;
  }

  intptr_t result = 0;
  for (size_t j = start; j < hex_str.length(); j++) {
    char c = hex_str[j];
    int digit = 0;
    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      digit = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      digit = c - 'A' + 10;
    } else {
      format_error(i.result, "hex: invalid hex character '%c'", c);
      return S_ERR;
    }
    result = result * 16 + digit;
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "%ld", (long)result);
  i.result = buf;
  return S_OK;
}

static Status cmd_oct(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("oct", argv, 2, 2)) {
    return S_ERR;
  }

  const string &oct_str = argv[1];
  if (oct_str.length() == 0) {
    format_error(i.result, "oct: empty string");
    return S_ERR;
  }

  // Parse octal string (with optional 0o prefix)
  size_t start = 0;
  if (oct_str.length() > 2 && oct_str[0] == '0' && (oct_str[1] == 'o' || oct_str[1] == 'O')) {
    start = 2;
  }

  intptr_t result = 0;
  for (size_t j = start; j < oct_str.length(); j++) {
    char c = oct_str[j];
    if (c >= '0' && c <= '7') {
      result = result * 8 + (c - '0');
    } else {
      format_error(i.result, "oct: invalid octal character '%c'", c);
      return S_ERR;
    }
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "%ld", (long)result);
  i.result = buf;
  return S_OK;
}

static Status cmd_bin(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("bin", argv, 2, 2)) {
    return S_ERR;
  }

  const string &bin_str = argv[1];
  if (bin_str.length() == 0) {
    format_error(i.result, "bin: empty string");
    return S_ERR;
  }

  // Parse binary string (with optional 0b prefix)
  size_t start = 0;
  if (bin_str.length() > 2 && bin_str[0] == '0' && (bin_str[1] == 'b' || bin_str[1] == 'B')) {
    start = 2;
  }

  intptr_t result = 0;
  for (size_t j = start; j < bin_str.length(); j++) {
    char c = bin_str[j];
    if (c == '0' || c == '1') {
      result = result * 2 + (c - '0');
    } else {
      format_error(i.result, "bin: invalid binary character '%c'", c);
      return S_ERR;
    }
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "%ld", (long)result);
  i.result = buf;
  return S_OK;
}

static Status cmd_eval(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("eval", argv, 2, 2)) {
    return S_ERR;
  }
  return i.eval(string_view(argv[1]));
}

void register_core_commands(Interp &i) {
  // I/O commands
  i.register_command("puts", cmd_puts, nullptr, "[puts string] => nil - Print string to output");

  // Variable commands
  i.register_command("set", cmd_set, nullptr, "[set var value] => value - Set variable to value");

  // Control flow commands
  i.register_command("if", cmd_if, nullptr,
                     "[if cond then else?] => any - Evaluate then-body if "
                     "condition is true, else-body otherwise");
  i.register_command("while", cmd_while, nullptr, "[while cond body] => nil - Execute body while condition is true");
  i.register_command("break", cmd_break, nullptr, "[break] => nil - Break out of innermost loop");
  i.register_command("continue", cmd_continue, nullptr, "[continue] => nil - Skip to next iteration of innermost loop");

  // Procedure commands
  i.register_command("proc", cmd_proc, nullptr, "[proc name args body] => nil - Define a new procedure");
  i.register_command("return", cmd_return, nullptr,
                     "[return value?] => any - Return from current procedure "
                     "with optional value");

  // Arithmetic commands
  i.register_command("+", cmd_add, nullptr, "[+ a:int b:int] => int - Add two integers");
  i.register_command("-", cmd_sub, nullptr, "[- a:int b:int] => int - Subtract b from a");
  i.register_command("*", cmd_mul, nullptr, "[* a:int b:int] => int - Multiply two integers");
  i.register_command("/", cmd_div, nullptr, "[/ a:int b:int] => int - Divide a by b (integer division)");

  // Comparison commands
  i.register_command("==", cmd_eq, nullptr, "[== a:int b:int] => bool - Test if a equals b (returns 1 or 0)");
  i.register_command("!=", cmd_ne, nullptr,
                     "[!= a:int b:int] => bool - Test if a is not equal to b "
                     "(returns 1 or 0)");
  i.register_command(">", cmd_gt, nullptr, "[> a:int b:int] => bool - Test if a is greater than b (returns 1 or 0)");
  i.register_command("<", cmd_lt, nullptr, "[< a:int b:int] => bool - Test if a is less than b (returns 1 or 0)");
  i.register_command(">=", cmd_gte, nullptr,
                     "[>= a:int b:int] => bool - Test if a is greater than or "
                     "equal to b (returns 1 or 0)");
  i.register_command("<=", cmd_lte, nullptr,
                     "[<= a:int b:int] => bool - Test if a is less than or "
                     "equal to b (returns 1 or 0)");

  // Help commands
  i.register_command("help", cmd_help, nullptr,
                     "[help cmd?] => nil - Show help for all commands or a specific command");
  i.register_command("commands", cmd_commands, nullptr, "[commands] => nil - List all available commands");

  // List commands
  i.register_command("list", cmd_list, nullptr, "[list elem1 elem2 ...] => list - Create a list from arguments");
  i.register_command("lindex", cmd_lindex, nullptr, "[lindex list index:int] => elem - Get element at index from list");
  i.register_command("lappend", cmd_lappend, nullptr,
                     "[lappend varName elem ...] => list - Append elements to list variable");
  i.register_command("llength", cmd_llength, nullptr, "[llength list] => int - Get the length of a list");
  i.register_command("lrange", cmd_lrange, nullptr,
                     "[lrange list start:int end:int] => list - Get range of elements from list");
  i.register_command("split", cmd_split, nullptr,
                     "[split string delimiter?] => list - Split string into list (default delimiter: space)");
  i.register_command("join", cmd_join, nullptr,
                     "[join list separator?] => string - Join list elements into string (default separator: space)");

  // Number conversion commands
  i.register_command("hex", cmd_hex, nullptr,
                     "[hex string] => int - Parse hexadecimal string to decimal (supports 0x prefix)");
  i.register_command("oct", cmd_oct, nullptr,
                     "[oct string] => int - Parse octal string to decimal (supports 0o prefix)");
  i.register_command("bin", cmd_bin, nullptr,
                     "[bin string] => int - Parse binary string to decimal (supports 0b prefix)");

  // Eval command
  i.register_command("eval", cmd_eval, nullptr, "[eval string] => any - Evaluate a Tcl string and return the result");
}

//
// MESSAGEPACK COMMANDS
//

static Status cmd_mp_reset(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("mp/reset", argv, 1, 1)) {
    return S_ERR;
  }
  if (!i.mpack_buffer_) {
    format_error(i.result, "mp/reset: MessagePack buffer not initialized");
    return S_ERR;
  }
  i.mpack_writer_.reset();
  return S_OK;
}

static Status cmd_mp_array(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("mp/array", argv, 2, 2)) {
    return S_ERR;
  }
  if (!i.mpack_buffer_) {
    format_error(i.result, "mp/array: MessagePack buffer not initialized");
    return S_ERR;
  }
  if (!i.int_check("mp/array", argv, 1)) {
    return S_ERR;
  }
  int count = atoi(argv[1].c_str());
  if (count < 0) {
    format_error(i.result, "mp/array: count must be non-negative");
    return S_ERR;
  }
  i.mpack_writer_.array((uint32_t)count);
  if (!i.mpack_writer_.ok()) {
    format_error(i.result, "mp/array: buffer overflow");
    return S_ERR;
  }
  return S_OK;
}

static Status cmd_mp_map(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("mp/map", argv, 2, 2)) {
    return S_ERR;
  }
  if (!i.mpack_buffer_) {
    format_error(i.result, "mp/map: MessagePack buffer not initialized");
    return S_ERR;
  }
  if (!i.int_check("mp/map", argv, 1)) {
    return S_ERR;
  }
  int count = atoi(argv[1].c_str());
  if (count < 0) {
    format_error(i.result, "mp/map: count must be non-negative");
    return S_ERR;
  }
  i.mpack_writer_.map((uint32_t)count);
  if (!i.mpack_writer_.ok()) {
    format_error(i.result, "mp/map: buffer overflow");
    return S_ERR;
  }
  return S_OK;
}

static Status cmd_mp_string(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("mp/string", argv, 2, 2)) {
    return S_ERR;
  }
  if (!i.mpack_buffer_) {
    format_error(i.result, "mp/string: MessagePack buffer not initialized");
    return S_ERR;
  }
  i.mpack_writer_.str(argv[1].c_str(), (uint32_t)argv[1].length());
  if (!i.mpack_writer_.ok()) {
    format_error(i.result, "mp/string: buffer overflow");
    return S_ERR;
  }
  return S_OK;
}

static Status cmd_mp_int(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("mp/int", argv, 2, 2)) {
    return S_ERR;
  }
  if (!i.mpack_buffer_) {
    format_error(i.result, "mp/int: MessagePack buffer not initialized");
    return S_ERR;
  }
  if (!i.int_check("mp/int", argv, 1)) {
    return S_ERR;
  }
  int32_t value = (int32_t)atoi(argv[1].c_str());
  i.mpack_writer_.pack(value);
  if (!i.mpack_writer_.ok()) {
    format_error(i.result, "mp/int: buffer overflow");
    return S_ERR;
  }
  return S_OK;
}

static Status cmd_mp_uint(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("mp/uint", argv, 2, 2)) {
    return S_ERR;
  }
  if (!i.mpack_buffer_) {
    format_error(i.result, "mp/uint: MessagePack buffer not initialized");
    return S_ERR;
  }
  // Check that it's a valid non-negative integer
  for (size_t j = 0; j < argv[1].length(); j++) {
    char c = argv[1][j];
    if (c < '0' || c > '9') {
      format_error(i.result, "mp/uint: argument must be a non-negative integer");
      return S_ERR;
    }
  }
  i.mpack_writer_.pack((uint32_t)atoi(argv[1].c_str()));
  if (!i.mpack_writer_.ok()) {
    format_error(i.result, "mp/uint: buffer overflow");
    return S_ERR;
  }
  return S_OK;
}

static Status cmd_mp_bool(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("mp/bool", argv, 2, 2)) {
    return S_ERR;
  }
  if (!i.mpack_buffer_) {
    format_error(i.result, "mp/bool: MessagePack buffer not initialized");
    return S_ERR;
  }
  bool value;
  if (argv[1].compare("0") == 0) {
    value = false;
  } else if (argv[1].compare("1") == 0) {
    value = true;
  } else {
    format_error(i.result, "mp/bool: argument must be 0 or 1");
    return S_ERR;
  }
  i.mpack_writer_.pack(value);
  if (!i.mpack_writer_.ok()) {
    format_error(i.result, "mp/bool: buffer overflow");
    return S_ERR;
  }
  return S_OK;
}

static Status cmd_mp_nil(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("mp/nil", argv, 1, 1)) {
    return S_ERR;
  }
  if (!i.mpack_buffer_) {
    format_error(i.result, "mp/nil: MessagePack buffer not initialized");
    return S_ERR;
  }
  i.mpack_writer_.nil();
  if (!i.mpack_writer_.ok()) {
    format_error(i.result, "mp/nil: buffer overflow");
    return S_ERR;
  }
  return S_OK;
}

static Status cmd_mp_print(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("mp/print", argv, 1, 1)) {
    return S_ERR;
  }
  if (!i.mpack_buffer_) {
    format_error(i.result, "mp/print: MessagePack buffer not initialized");
    return S_ERR;
  }
  if (!i.mpack_writer_.ok()) {
    format_error(i.result, "mp/print: MessagePack writer is in error state");
    return S_ERR;
  }
  // Use mpack_print with oputchar callback (works in both OT_POSIX and
  // non-POSIX)
  mpack_print((const char *)i.mpack_writer_.data(), i.mpack_writer_.size(), [](char ch) -> int {
    oputchar(ch);
    return 1;
  });

  oputchar('\n');
  return S_OK;
}

static Status cmd_mp_size(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("mp/size", argv, 1, 1)) {
    return S_ERR;
  }
  if (!i.mpack_buffer_) {
    format_error(i.result, "mp/size: MessagePack buffer not initialized");
    return S_ERR;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", (int)i.mpack_writer_.size());
  i.result = buf;
  return S_OK;
}

static Status cmd_mp_hex(Interp &i, vector<string> &argv, ProcPrivdata *privdata) {
  if (!i.arity_check("mp/hex", argv, 1, 1)) {
    return S_ERR;
  }
  if (!i.mpack_buffer_) {
    format_error(i.result, "mp/hex: MessagePack buffer not initialized");
    return S_ERR;
  }
  const unsigned char *data = (const unsigned char *)i.mpack_writer_.data();
  uint32_t size = i.mpack_writer_.size();
  i.result.clear();
  const char *hexchars = "0123456789abcdef";
  for (uint32_t j = 0; j < size; j++) {
    i.result += hexchars[(data[j] >> 4) & 0xf];
    i.result += hexchars[data[j] & 0xf];
    i.result += ' ';
  }
  return S_OK;
}

void Interp::register_mpack_functions(char *buffer, size_t size) {
  mpack_buffer_ = buffer;
  mpack_buffer_size_ = size;
  mpack_writer_.init(buffer, size);

  register_command("mp/reset", cmd_mp_reset, nullptr, "[mp/reset] => nil - Reset MessagePack buffer to empty state");
  register_command("mp/array", cmd_mp_array, nullptr,
                   "[mp/array count:int] => nil - Begin MessagePack array with "
                   "given element count");
  register_command("mp/map", cmd_mp_map, nullptr,
                   "[mp/map count:int] => nil - Begin MessagePack map with "
                   "given key-value pair count");
  register_command("mp/string", cmd_mp_string, nullptr, "[mp/string str] => nil - Write string to MessagePack buffer");
  register_command("mp/int", cmd_mp_int, nullptr,
                   "[mp/int value:int] => nil - Write signed integer to MessagePack buffer");
  register_command("mp/uint", cmd_mp_uint, nullptr,
                   "[mp/uint value:uint] => nil - Write unsigned integer to "
                   "MessagePack buffer");
  register_command("mp/bool", cmd_mp_bool, nullptr,
                   "[mp/bool value:bool] => nil - Write boolean (0 or 1) to "
                   "MessagePack buffer");
  register_command("mp/nil", cmd_mp_nil, nullptr, "[mp/nil] => nil - Write nil value to MessagePack buffer");
  register_command("mp/print", cmd_mp_print, nullptr,
                   "[mp/print] => nil - Print human-readable representation of "
                   "MessagePack buffer");
  register_command("mp/size", cmd_mp_size, nullptr,
                   "[mp/size] => int - Return current size of MessagePack buffer in bytes");
  register_command("mp/hex", cmd_mp_hex, nullptr,
                   "[mp/hex] => string - Return hexadecimal representation of "
                   "MessagePack buffer");
}

} // namespace tcl
