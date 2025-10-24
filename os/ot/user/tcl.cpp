// tcl.cpp - implementation of minimal dependency Tcl interpreter

#include "ot/user/tcl.h"
#include "ot/shared/mpack-utils.hpp"
#include "ot/shared/mpack-writer.hpp"

namespace tcl {

//
// STRING IMPLEMENTATION
//

void string::ensure_capacity(size_t new_cap) {
  if (new_cap <= cap_)
    return;
  size_t alloc_cap = cap_ == 0 ? 16 : cap_;
  while (alloc_cap < new_cap)
    alloc_cap *= 2;
  char *new_data = (char *)tcl_realloc(data_, alloc_cap);
  data_ = new_data;
  cap_ = alloc_cap;
}

string::string() : data_(nullptr), len_(0), cap_(0) {}

string::string(const char *s) : data_(nullptr), len_(0), cap_(0) {
  if (s) {
    len_ = strlen(s);
    ensure_capacity(len_ + 1);
    memcpy(data_, s, len_);
    data_[len_] = '\0';
  }
}

string::string(const char *s, size_t n) : data_(nullptr), len_(0), cap_(0) {
  if (s && n > 0) {
    len_ = n;
    ensure_capacity(len_ + 1);
    memcpy(data_, s, len_);
    data_[len_] = '\0';
  }
}

string::string(const string &other) : data_(nullptr), len_(0), cap_(0) {
  if (other.len_ > 0) {
    len_ = other.len_;
    ensure_capacity(len_ + 1);
    memcpy(data_, other.data_, len_);
    data_[len_] = '\0';
  }
}

string::string(string &&other) noexcept
    : data_(other.data_), len_(other.len_), cap_(other.cap_) {
  other.data_ = nullptr;
  other.len_ = 0;
  other.cap_ = 0;
}

string::~string() {
  if (data_)
    tcl_free(data_);
}

string &string::operator=(const string &other) {
  if (this != &other) {
    clear();
    if (other.len_ > 0) {
      len_ = other.len_;
      ensure_capacity(len_ + 1);
      memcpy(data_, other.data_, len_);
      data_[len_] = '\0';
    }
  }
  return *this;
}

string &string::operator=(string &&other) noexcept {
  if (this != &other) {
    if (data_)
      tcl_free(data_);
    data_ = other.data_;
    len_ = other.len_;
    cap_ = other.cap_;
    other.data_ = nullptr;
    other.len_ = 0;
    other.cap_ = 0;
  }
  return *this;
}

string &string::operator=(const char *s) {
  clear();
  if (s) {
    len_ = strlen(s);
    ensure_capacity(len_ + 1);
    memcpy(data_, s, len_);
    data_[len_] = '\0';
  }
  return *this;
}

void string::clear() {
  len_ = 0;
  if (data_)
    data_[0] = '\0';
}

void string::reserve(size_t new_cap) { ensure_capacity(new_cap); }

void string::append(const char *s, size_t n) {
  if (s && n > 0) {
    ensure_capacity(len_ + n + 1);
    memcpy(data_ + len_, s, n);
    len_ += n;
    data_[len_] = '\0';
  }
}

void string::append(const char *s) {
  if (s)
    append(s, strlen(s));
}

void string::append(const string &s) { append(s.data_, s.len_); }

void string::push_back(char c) {
  ensure_capacity(len_ + 2);
  data_[len_++] = c;
  data_[len_] = '\0';
}

string &string::operator+=(const char *s) {
  append(s);
  return *this;
}

string &string::operator+=(const string &s) {
  append(s);
  return *this;
}

string &string::operator+=(char c) {
  push_back(c);
  return *this;
}

int string::compare(const char *s) const {
  if (!data_ && !s)
    return 0;
  if (!data_)
    return -1;
  if (!s)
    return 1;
  return strcmp(data_, s);
}

int string::compare(const string &s) const { return compare(s.data_); }

int string::compare(const string_view &s) const {
  if (!data_ && !s.data_)
    return 0;
  if (!data_)
    return -1;
  if (!s.data_)
    return 1;
  size_t minlen = len_ < s.len_ ? len_ : s.len_;
  int cmp = memcmp(data_, s.data_, minlen);
  if (cmp == 0)
    return len_ < s.len_ ? -1 : (len_ > s.len_ ? 1 : 0);
  return cmp;
}

string string::substr(size_t pos, size_t len) const {
  if (pos > len_)
    pos = len_;
  if (pos + len > len_)
    len = len_ - pos;
  return string(data_ + pos, len);
}

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
  ovsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  result = buffer;
}

//
// PARSER IMPLEMENTATION
//

Parser::Parser(const string_view &body_, bool trace_parser_)
    : body(body_), cursor(0), begin(0), end(0), trace_parser(trace_parser_),
      in_string(false), in_brace(false), in_quote(false), brace_level(0),
      token(TK_EOL), terminating_char(0) {}

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
    case '\"': {
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

ProcPrivdata::ProcPrivdata(string *args_, string *body_)
    : args(args_), body(body_) {}

ProcPrivdata::~ProcPrivdata() {
  tcl_delete(args);
  tcl_delete(body);
}

//
// CMD IMPLEMENTATION
//

Cmd::Cmd(const string &name_, cmd_func_t func_, ProcPrivdata *privdata_)
    : name(name_), func(func_), privdata(privdata_) {}

Cmd::~Cmd() {
  if (privdata)
    tcl_delete(privdata);
}

//
// VAR IMPLEMENTATION
//

Var::~Var() {
  tcl_delete(name);
  tcl_delete(val);
}

//
// CALLFRAME IMPLEMENTATION
//

CallFrame::~CallFrame() {
  for (Var *v : vars) {
    tcl_delete(v);
  }
}

//
// INTERP IMPLEMENTATION
//

Interp::Interp()
    : trace_parser(false), mpack_buffer_(nullptr), mpack_buffer_size_(0) {
  callframes.push_back(tcl_new<CallFrame>());
}

Interp::~Interp() {
  for (CallFrame *cf : callframes) {
    tcl_delete(cf);
  }
  for (Cmd *c : commands) {
    tcl_delete(c);
  }
}

void Interp::drop_call_frame() {
  CallFrame *cf = callframes.back();
  callframes.pop_back();
  tcl_delete(cf);
}

Cmd *Interp::get_command(const string &name) const {
  for (Cmd *c : commands) {
    if (c->name.compare(name) == 0) {
      return c;
    }
  }
  return nullptr;
}

Status Interp::register_command(const string &name, cmd_func_t fn,
                                ProcPrivdata *privdata) {
  if (get_command(name) != nullptr) {
    format_error(result, "command already defined: '%s'", name.c_str());
    return S_ERR;
  }
  commands.push_back(tcl_new<Cmd>(name, fn, privdata));
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
    tcl_delete(v->val);
    v->val = tcl_new<string>(val);
  } else {
    v = tcl_new<Var>();
    v->name = tcl_new<string>(name);
    v->val = tcl_new<string>(val);
    callframes.back()->vars.push_back(v);
  }
  return S_OK;
}

bool Interp::arity_check(const string &name, const vector<string> &argv,
                         size_t min, size_t max) {
  if (min == max && argv.size() != min) {
    format_error(result, "wrong number of args for %s (expected %zu)",
                 name.c_str(), min);
    return false;
  }
  if (argv.size() < min || argv.size() > max) {
    format_error(result, "[%s]: wrong number of args (expected %zu to %zu)",
                 name.c_str(), min, max);
    return false;
  }
  return true;
}

bool Interp::int_check(const string &name, const vector<string> &argv,
                       size_t idx) {
  const string &arg = argv[idx];
  for (size_t i = 0; i < arg.length(); i++) {
    char c = arg[i];
    if (i == 0 && (c == '-' || c == '+'))
      continue;
    if (c < '0' || c > '9') {
      format_error(result, "[%s]: argument %zu is not an integer", name.c_str(),
                   idx);
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
        format_error(result, "variable not found: '%.*s'", (int)t.length(),
                     t.data());
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

    if (prevtype == TK_SEP || prevtype == TK_EOL) {
      argv.push_back(string(t.data(), t.length()));
    } else {
      argv[argv.size() - 1] += string(t.data(), t.length());
    }
    prevtype = token;
  }
  return S_OK;
}

//
// CALL_PROC IMPLEMENTATION
//

Status call_proc(Interp &i, vector<string> &argv, ProcPrivdata *pd) {
  CallFrame *cf = tcl_new<CallFrame>();
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
    format_error(i.result,
                 "wrong number of arguments for %s got %zu expected %zu",
                 argv[0].c_str(), argv.size(), arity);
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

static Status cmd_puts(Interp &i, vector<string> &argv,
                       ProcPrivdata *privdata) {
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

static Status cmd_while(Interp &i, vector<string> &argv,
                        ProcPrivdata *privdata) {
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

static Status cmd_break(Interp &i, vector<string> &argv,
                        ProcPrivdata *privdata) {
  if (!i.arity_check("break", argv, 1, 1)) {
    return S_ERR;
  }
  return S_BREAK;
}

static Status cmd_continue(Interp &i, vector<string> &argv,
                           ProcPrivdata *privdata) {
  if (!i.arity_check("continue", argv, 1, 1)) {
    return S_ERR;
  }
  return S_CONTINUE;
}

static Status cmd_proc(Interp &i, vector<string> &argv,
                       ProcPrivdata *privdata) {
  if (!i.arity_check("proc", argv, 4, 4)) {
    return S_ERR;
  }
  ProcPrivdata *ppd =
      tcl_new<ProcPrivdata>(tcl_new<string>(argv[2]), tcl_new<string>(argv[3]));
  i.register_command(argv[1], call_proc, ppd);
  return S_OK;
}

static Status cmd_return(Interp &i, vector<string> &argv,
                         ProcPrivdata *privdata) {
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
  osnprintf(buf, sizeof(buf), "%d", result);
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
  osnprintf(buf, sizeof(buf), "%d", result);
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
  osnprintf(buf, sizeof(buf), "%d", result);
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
  osnprintf(buf, sizeof(buf), "%d", result);
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
  osnprintf(buf, sizeof(buf), "%d", result);
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
  osnprintf(buf, sizeof(buf), "%d", result);
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
  osnprintf(buf, sizeof(buf), "%d", result);
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
  osnprintf(buf, sizeof(buf), "%d", result);
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
  osnprintf(buf, sizeof(buf), "%d", result);
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
  osnprintf(buf, sizeof(buf), "%d", result);
  i.result = buf;
  return S_OK;
}

void register_core_commands(Interp &i) {
  i.register_command("puts", cmd_puts);
  i.register_command("set", cmd_set);
  i.register_command("if", cmd_if);
  i.register_command("while", cmd_while);
  i.register_command("break", cmd_break);
  i.register_command("continue", cmd_continue);
  i.register_command("proc", cmd_proc);
  i.register_command("return", cmd_return);
  i.register_command("+", cmd_add);
  i.register_command("-", cmd_sub);
  i.register_command("*", cmd_mul);
  i.register_command("/", cmd_div);
  i.register_command("==", cmd_eq);
  i.register_command("!=", cmd_ne);
  i.register_command(">", cmd_gt);
  i.register_command("<", cmd_lt);
  i.register_command(">=", cmd_gte);
  i.register_command("<=", cmd_lte);
}

//
// MESSAGEPACK COMMANDS
//

static Status cmd_mp_reset(Interp &i, vector<string> &argv,
                           ProcPrivdata *privdata) {
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

static Status cmd_mp_array(Interp &i, vector<string> &argv,
                           ProcPrivdata *privdata) {
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

static Status cmd_mp_map(Interp &i, vector<string> &argv,
                         ProcPrivdata *privdata) {
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

static Status cmd_mp_string(Interp &i, vector<string> &argv,
                            ProcPrivdata *privdata) {
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

static Status cmd_mp_int(Interp &i, vector<string> &argv,
                         ProcPrivdata *privdata) {
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

static Status cmd_mp_uint(Interp &i, vector<string> &argv,
                          ProcPrivdata *privdata) {
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
      format_error(i.result,
                   "mp/uint: argument must be a non-negative integer");
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

static Status cmd_mp_bool(Interp &i, vector<string> &argv,
                          ProcPrivdata *privdata) {
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

static Status cmd_mp_nil(Interp &i, vector<string> &argv,
                         ProcPrivdata *privdata) {
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

static Status cmd_mp_print(Interp &i, vector<string> &argv,
                           ProcPrivdata *privdata) {
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
  mpack_print((const char *)i.mpack_writer_.data(), i.mpack_writer_.size(),
              [](char ch) -> int {
                oputchar(ch);
                return 1;
              });

  oputchar('\n');
  return S_OK;
}

static Status cmd_mp_size(Interp &i, vector<string> &argv,
                          ProcPrivdata *privdata) {
  if (!i.arity_check("mp/size", argv, 1, 1)) {
    return S_ERR;
  }
  if (!i.mpack_buffer_) {
    format_error(i.result, "mp/size: MessagePack buffer not initialized");
    return S_ERR;
  }
  char buf[32];
  osnprintf(buf, sizeof(buf), "%d", (int)i.mpack_writer_.size());
  i.result = buf;
  return S_OK;
}

static Status cmd_mp_hex(Interp &i, vector<string> &argv,
                         ProcPrivdata *privdata) {
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

void Interp::register_mpack_functions(void *buffer, size_t size) {
  mpack_buffer_ = buffer;
  mpack_buffer_size_ = size;
  mpack_writer_.init(buffer, size);

  register_command("mp/reset", cmd_mp_reset);
  register_command("mp/array", cmd_mp_array);
  register_command("mp/map", cmd_mp_map);
  register_command("mp/string", cmd_mp_string);
  register_command("mp/int", cmd_mp_int);
  register_command("mp/uint", cmd_mp_uint);
  register_command("mp/bool", cmd_mp_bool);
  register_command("mp/nil", cmd_mp_nil);
  register_command("mp/print", cmd_mp_print);
  register_command("mp/size", cmd_mp_size);
  register_command("mp/hex", cmd_mp_hex);
}

} // namespace tcl
