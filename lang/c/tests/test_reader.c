#include "ctest.h"

#include "value.h"
#include "state.h"
#include "reader.h"
#include "printer.h"

static State* test_vm(void) {
  static State* vm = nullptr;
  if (!vm) {
    StateConfig cfg = state_config_default();
    cfg.heapBytes = 1 << 20;
    cfg.stackSlots = 1024;
    cfg.maxDepth = 256;
    vm = state_create(&cfg);
  }
  return vm;
}

// Read a single form from src.
static Value read1(const char* src) {
  Reader r;
  reader_init(&r, test_vm(), src, (u32)strlen(src), "<test>");
  return reader_next(&r);
}

// repr into a static NUL-terminated buffer (valid until the next call).
static const char* repr_of(Value v) {
  static Buf b;
  buf_clear(&b);
  print_repr(test_vm(), v, &b);
  vec_push(&b, '\0');
  b.len--;  // keep len as the string length
  return b.data;
}

// read -> repr as a string
static const char* rr(const char* src) { return repr_of(read1(src)); }

static bool read_errors(const char* src) { return read1(src).tag == Tag_Unwind; }

TEST(atoms_nil_and_booleans) {
  CHECK(read1("nil").tag == Tag_Nil);
  CHECK(read1("#t").tag == Tag_True);
  CHECK(read1("#true").tag == Tag_True);
  CHECK(read1("true").tag == Tag_True);
  CHECK(read1("#f").tag == Tag_False);
  CHECK(read1("#false").tag == Tag_False);
  CHECK(read1("false").tag == Tag_False);
}

TEST(hash_prefix_is_reserved) {
  CHECK(read_errors("#x"));
  CHECK(read_errors("#truthy"));
  CHECK(read_errors("#"));
  CHECK(read_errors("#<fn>"));
}

TEST(keywords) {
  Value v = read1(":foo");
  CHECK(v.tag == Tag_Keyword);
  CHECK_STR(rr(":foo"), ":foo");
  CHECK(read_errors(":"));  // bare colon
  CHECK_STR(rr(":a/b"), ":a/b");
}

TEST(numbers_integers) {
  CHECK_STR(rr("42"), "42");
  CHECK_STR(rr("-7"), "-7");
  CHECK_STR(rr("+7"), "7");
  CHECK_STR(rr("0xff"), "255");
  CHECK_STR(rr("-0x10"), "-16");
  CHECK_STR(rr("9223372036854775807"), "9223372036854775807");
  CHECK_STR(rr("-9223372036854775808"), "-9223372036854775808");
}

TEST(numbers_out_of_range_integer_literal_is_a_read_error) {
  CHECK(read_errors("9223372036854775808"));
  CHECK(read_errors("-9223372036854775809"));
  CHECK(read_errors("0x10000000000000000"));
}

TEST(numbers_floats) {
  Value v = read1("3.5");
  CHECK(v.tag == Tag_Float);
  CHECK_STR(rr("3.5"), "3.5");
  CHECK_STR(rr("-.5"), "-0.5");
  CHECK_STR(rr("1e3"), "1000.0");
  CHECK_STR(rr("3.0"), "3.0");  // integral floats keep .0
  CHECK_STR(rr("0.1"), "0.1");  // shortest round-trip
}

TEST(numbers_starts_numerically_must_parse_as_a_number) {
  CHECK(read_errors("12abc"));
  CHECK(read_errors("1.2.3"));
  CHECK(read_errors(".."));
  CHECK(read_errors("0x"));
  CHECK(read_errors("0xzz"));
  CHECK(read_errors("-1x"));
}

TEST(numbers_things_that_look_numeric_but_are_symbols) {
  CHECK(read1("+").tag == Tag_Symbol);
  CHECK(read1("-").tag == Tag_Symbol);
  CHECK(read1("->").tag == Tag_Symbol);
  CHECK(read1(".").tag == Tag_Symbol);  // lone dot outside a list
}

TEST(symbols) {
  CHECK(read1("foo-bar!").tag == Tag_Symbol);
  CHECK_STR(rr("foo-bar!"), "foo-bar!");
  CHECK_STR(rr("term/write"), "term/write");
  CHECK_STR(rr("/"), "/");
}

TEST(strings) {
  CHECK_STR(rr("\"hi\""), "\"hi\"");
  CHECK_STR(rr("\"a\\nb\""), "\"a\\nb\"");
  CHECK_STR(rr("\"tab\\there\""), "\"tab\\there\"");
  CHECK_STR(rr("\"q\\\"q\""), "\"q\\\"q\"");
  CHECK_STR(rr("\"bs\\\\bs\""), "\"bs\\\\bs\"");
  CHECK_STR(rr("\"esc\\e\""), "\"esc\\e\"");
  CHECK_STR(rr("\"nul\\0nul\""), "\"nul\\0nul\"");
  CHECK_STR(rr("\"multi\nline\""), "\"multi\\nline\"");  // literal newline ok
  CHECK(read_errors("\"bad\\qesc\""));                   // unknown escape
  CHECK(read_errors("\"unterminated"));
  // display renders raw
  Buf b = {0};
  print_display(test_vm(), read1("\"a\\tb\""), &b);
  CHECK_MEM(b.data, b.len, "a\tb");
  buf_deinit(&b);
}

TEST(lists_and_pairs) {
  CHECK_STR(rr("()"), "()");
  CHECK(read1("()").tag == Tag_Null);
  CHECK_STR(rr("(a b c)"), "(a b c)");
  CHECK_STR(rr("(a . b)"), "(a . b)");
  CHECK_STR(rr("(a b . c)"), "(a b . c)");
  CHECK_STR(rr("(1 (2 3) 4)"), "(1 (2 3) 4)");
}

TEST(lists_dotted_tail_errors) {
  CHECK(read_errors("(. b)"));      // no preceding element
  CHECK(read_errors("(a . b c)"));  // content after tail
  CHECK(read_errors("(a .)"));      // no tail form
  CHECK(read_errors("(a b"));       // unterminated
  CHECK(read_errors(")"));
}

TEST(collection_literals_read_as_lists) {
  // '[1 2] is the LIST (array 1 2), not an array (spec 1.7)
  CHECK_STR(rr("[1 2]"), "(array 1 2)");
  Value v = read1("[1 2]");
  CHECK(v.tag == Tag_Pair);
  CHECK_STR(rr("{:a 1}"), "(table :a 1)");
  CHECK_STR(rr("[]"), "(array)");
  CHECK_STR(rr("{}"), "(table)");
  CHECK_STR(rr("[a [b] {c d}]"), "(array a (array b) (table c d))");
  CHECK_STR(rr("'[1 2]"), "(quote (array 1 2))");
}

TEST(quote_sugar) {
  CHECK_STR(rr("'x"), "(quote x)");
  CHECK_STR(rr("`x"), "(quasiquote x)");
  CHECK_STR(rr(",x"), "(unquote x)");
  CHECK_STR(rr(",@x"), "(unquote-splicing x)");
  CHECK_STR(rr("`(a ,b ,@c)"), "(quasiquote (a (unquote b) (unquote-splicing c)))");
  CHECK_STR(rr("''x"), "(quote (quote x))");
  CHECK(read_errors("'"));  // nothing to quote
}

TEST(comments_and_whitespace) {
  CHECK_STR(rr("; hi\n42"), "42");
  CHECK_STR(rr("(a ; mid\n b)"), "(a b)");
}

TEST(multiple_forms_and_eof) {
  State* vm = test_vm();
  const char* src = "1 2 3";
  Reader r;
  reader_init(&r, vm, src, (u32)strlen(src), "<test>");
  CHECK_STR(repr_of(reader_next(&r)), "1");
  CHECK(!reader_at_eof(&r));
  CHECK_STR(repr_of(reader_next(&r)), "2");
  CHECK_STR(repr_of(reader_next(&r)), "3");
  Value v = reader_next(&r);
  CHECK(v.tag == Tag_Nil);
  CHECK(reader_at_eof(&r));
}

TEST(incomplete_input_is_distinct_from_a_final_read_error) {
  State* vm = test_vm();
  const char* incomplete[] = {"(a b", "[1 2", "{:a 1", "\"open", "'", "(a . b"};
  for (u32 i = 0; i < sizeof(incomplete) / sizeof(incomplete[0]); i++) {
    const char* src = incomplete[i];
    Reader r;
    reader_init(&r, vm, src, (u32)strlen(src), "<test>");
    CHECK(reader_next(&r).tag == Tag_Unwind);
    CHECK(reader_incomplete(&r));
    state_cancel_unwind(vm);
  }

  const char* invalid[] = {")", "(a]", "\"bad\\q\"", "(a . b c)"};
  for (u32 i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    const char* src = invalid[i];
    Reader r;
    reader_init(&r, vm, src, (u32)strlen(src), "<test>");
    CHECK(reader_next(&r).tag == Tag_Unwind);
    CHECK(!reader_incomplete(&r));
    state_cancel_unwind(vm);
  }
}

TEST(read_print_read_fixpoint) {
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
  for (u32 i = 0; i < sizeof(corpus) / sizeof(corpus[0]); i++) {
    const char* src = corpus[i];
    Value v1 = read1(src);
    CHECK(v1.tag != Tag_Unwind);
    if (v1.tag == Tag_Unwind) {
      CTEST_FAIL("corpus form failed to read: %s", src);
      continue;
    }
    char s1[512];
    snprintf(s1, sizeof(s1), "%s", repr_of(v1));
    Value v2 = read1(s1);
    CHECK(v2.tag != Tag_Unwind);
    if (v2.tag == Tag_Unwind) {
      CTEST_FAIL("repr failed to re-read: %s", s1);
      continue;
    }
    const char* s2 = repr_of(v2);
    if (strcmp(s1, s2) != 0) CTEST_FAIL("fixpoint %s: \"%s\" != \"%s\"", src, s1, s2);
  }
  // sugar and bracket forms normalize to their list spellings, then fix
  const char* sugared[] = {"'x", "`(a ,b)", "[1 [2] 3]", "{:k 'v}"};
  for (u32 i = 0; i < sizeof(sugared) / sizeof(sugared[0]); i++) {
    const char* src = sugared[i];
    char s1[512];
    snprintf(s1, sizeof(s1), "%s", rr(src));
    if (strcmp(rr(s1), s1) != 0) CTEST_FAIL("sugared fixpoint %s: \"%s\"", src, s1);
  }
}
