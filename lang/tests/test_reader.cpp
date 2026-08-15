#include "doctest.h"
#include <string>
#include <cstring>

#include "../src/value.hpp"
#include "../src/vm.hpp"
#include "../src/reader.hpp"
#include "../src/printer.hpp"

using namespace ot;

static Vm* test_vm() {
  static Vm* vm = nullptr;
  if (!vm) {
    VmConfig cfg{};
    cfg.heapBytes = 1 << 20;
    cfg.stackSlots = 1024;
    cfg.maxDepth = 256;
    vm = Vm::create(cfg);
  }
  return vm;
}

// Read a single form from src.
static Value read1(const char* src) {
  Vm& vm = *test_vm();
  Reader r(vm, src, (u32)strlen(src), "<test>");
  return r.next();
}

static std::string repr_of(Value v) {
  Buf b;
  print_repr(*test_vm(), v, b);
  return std::string(b.data ? b.data : "", b.len);
}

// read -> repr as a string
static std::string rr(const char* src) { return repr_of(read1(src)); }

static bool read_errors(const char* src) { return read1(src).tag == Tag::Unwind; }

TEST_CASE("atoms: nil and booleans") {
  CHECK(read1("nil").tag == Tag::Nil);
  CHECK(read1("#t").tag == Tag::True);
  CHECK(read1("#true").tag == Tag::True);
  CHECK(read1("true").tag == Tag::True);
  CHECK(read1("#f").tag == Tag::False);
  CHECK(read1("#false").tag == Tag::False);
  CHECK(read1("false").tag == Tag::False);
}

TEST_CASE("# prefix is reserved") {
  CHECK(read_errors("#x"));
  CHECK(read_errors("#truthy"));
  CHECK(read_errors("#"));
  CHECK(read_errors("#<fn>"));
}

TEST_CASE("keywords") {
  Value v = read1(":foo");
  CHECK(v.tag == Tag::Keyword);
  CHECK(rr(":foo") == ":foo");
  CHECK(read_errors(":"));  // bare colon
  CHECK(rr(":a/b") == ":a/b");
}

TEST_CASE("numbers") {
  SUBCASE("integers") {
    CHECK(rr("42") == "42");
    CHECK(rr("-7") == "-7");
    CHECK(rr("+7") == "7");
    CHECK(rr("0xff") == "255");
    CHECK(rr("-0x10") == "-16");
    CHECK(rr("9223372036854775807") == "9223372036854775807");
    CHECK(rr("-9223372036854775808") == "-9223372036854775808");
  }
  SUBCASE("out-of-range integer literal is a read error") {
    CHECK(read_errors("9223372036854775808"));
    CHECK(read_errors("-9223372036854775809"));
    CHECK(read_errors("0x10000000000000000"));
  }
  SUBCASE("floats") {
    Value v = read1("3.5");
    CHECK(v.tag == Tag::Float);
    CHECK(rr("3.5") == "3.5");
    CHECK(rr("-.5") == "-0.5");
    CHECK(rr("1e3") == "1000.0");
    CHECK(rr("3.0") == "3.0");  // integral floats keep .0
    CHECK(rr("0.1") == "0.1");  // shortest round-trip
  }
  SUBCASE("starts-numerically must parse as a number") {
    CHECK(read_errors("12abc"));
    CHECK(read_errors("1.2.3"));
    CHECK(read_errors(".."));
    CHECK(read_errors("0x"));
    CHECK(read_errors("0xzz"));
    CHECK(read_errors("-1x"));
  }
  SUBCASE("things that look numeric but are symbols") {
    CHECK(read1("+").tag == Tag::Symbol);
    CHECK(read1("-").tag == Tag::Symbol);
    CHECK(read1("->").tag == Tag::Symbol);
    CHECK(read1(".").tag == Tag::Symbol);  // lone dot outside a list
  }
}

TEST_CASE("symbols") {
  CHECK(read1("foo-bar!").tag == Tag::Symbol);
  CHECK(rr("foo-bar!") == "foo-bar!");
  CHECK(rr("term/write") == "term/write");
  CHECK(rr("/") == "/");
}

TEST_CASE("strings") {
  CHECK(rr("\"hi\"") == "\"hi\"");
  CHECK(rr("\"a\\nb\"") == "\"a\\nb\"");
  CHECK(rr("\"tab\\there\"") == "\"tab\\there\"");
  CHECK(rr("\"q\\\"q\"") == "\"q\\\"q\"");
  CHECK(rr("\"bs\\\\bs\"") == "\"bs\\\\bs\"");
  CHECK(rr("\"esc\\e\"") == "\"esc\\e\"");
  CHECK(rr("\"nul\\0nul\"") == "\"nul\\0nul\"");
  CHECK(rr("\"multi\nline\"") == "\"multi\\nline\"");  // literal newline ok
  CHECK(read_errors("\"bad\\qesc\""));                 // unknown escape
  CHECK(read_errors("\"unterminated"));
  // display renders raw
  Buf b;
  print_display(*test_vm(), read1("\"a\\tb\""), b);
  CHECK(std::string(b.data, b.len) == "a\tb");
}

TEST_CASE("lists and pairs") {
  CHECK(rr("()") == "()");
  CHECK(read1("()").tag == Tag::Null);
  CHECK(rr("(a b c)") == "(a b c)");
  CHECK(rr("(a . b)") == "(a . b)");
  CHECK(rr("(a b . c)") == "(a b . c)");
  CHECK(rr("(1 (2 3) 4)") == "(1 (2 3) 4)");
  SUBCASE("dotted-tail errors") {
    CHECK(read_errors("(. b)"));      // no preceding element
    CHECK(read_errors("(a . b c)"));  // content after tail
    CHECK(read_errors("(a .)"));      // no tail form
    CHECK(read_errors("(a b"));       // unterminated
    CHECK(read_errors(")"));
  }
}

TEST_CASE("collection literals read as lists") {
  // '[1 2] is the LIST (array 1 2), not an array (spec 1.7)
  CHECK(rr("[1 2]") == "(array 1 2)");
  Value v = read1("[1 2]");
  CHECK(v.tag == Tag::Pair);
  CHECK(rr("{:a 1}") == "(table :a 1)");
  CHECK(rr("[]") == "(array)");
  CHECK(rr("{}") == "(table)");
  CHECK(rr("[a [b] {c d}]") == "(array a (array b) (table c d))");
  CHECK(rr("'[1 2]") == "(quote (array 1 2))");
}

TEST_CASE("quote sugar") {
  CHECK(rr("'x") == "(quote x)");
  CHECK(rr("`x") == "(quasiquote x)");
  CHECK(rr(",x") == "(unquote x)");
  CHECK(rr(",@x") == "(unquote-splicing x)");
  CHECK(rr("`(a ,b ,@c)") == "(quasiquote (a (unquote b) (unquote-splicing c)))");
  CHECK(rr("''x") == "(quote (quote x))");
  CHECK(read_errors("'"));  // nothing to quote
}

TEST_CASE("comments and whitespace") {
  CHECK(rr("; hi\n42") == "42");
  CHECK(rr("(a ; mid\n b)") == "(a b)");
}

TEST_CASE("multiple forms and eof") {
  Vm& vm = *test_vm();
  const char* src = "1 2 3";
  Reader r(vm, src, (u32)strlen(src), "<test>");
  CHECK(repr_of(r.next()) == "1");
  CHECK(!r.atEof());
  CHECK(repr_of(r.next()) == "2");
  CHECK(repr_of(r.next()) == "3");
  Value v = r.next();
  CHECK(v.tag == Tag::Nil);
  CHECK(r.atEof());
}

TEST_CASE("read -> print -> read fixpoint") {
  const char* corpus[] = {
      "nil",
      "#t",
      "#f",
      "()",
      "42",
      "-7",
      "255",
      "3.5",
      "-0.5",
      "1000.0",
      "0.1",
      "1e300",
      ":kw",
      "sym",
      "foo-bar!",
      "/",
      "->",
      "\"hi\"",
      "\"a\\nb\\t\\\\\\\"\\e\\0\"",
      "(a b c)",
      "(a . b)",
      "(a b . c)",
      "(1 (2 (3)) 4)",
      "(array 1 2)",
      "(table :a 1 :b (array))",
      "(quote x)",
      "(quasiquote (a (unquote b) (unquote-splicing c)))",
      "(define (f x) (+ x 1))",
  };
  for (const char* src : corpus) {
    CAPTURE(src);
    Value v1 = read1(src);
    REQUIRE(v1.tag != Tag::Unwind);
    std::string s1 = repr_of(v1);
    Value v2 = read1(s1.c_str());
    REQUIRE(v2.tag != Tag::Unwind);
    std::string s2 = repr_of(v2);
    CHECK(s1 == s2);
  }
  // sugar and bracket forms normalize to their list spellings, then fix
  const char* sugared[] = {"'x", "`(a ,b)", "[1 [2] 3]", "{:k 'v}"};
  for (const char* src : sugared) {
    CAPTURE(src);
    std::string s1 = rr(src);
    CHECK(rr(s1.c_str()) == s1);
  }
}
