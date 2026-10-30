#include "helpers.h"
#include <string.h>

/* Each step relies on the bindings and libraries left by the ones before it. */
TEST(reload_releases_bindings_and_memory) {
  ot_state* rt = new_runtime(16384, 4096);
  ot_gc_stress(rt, true);
  char source[1024];
  snprintf(source, sizeof(source),
           "(def lib (otium.ffi/open-library '(\"%s\"))) "
           "(def saved (otium.ffi/bind lib \"echo_int\" '(int) 'int #t))",
           TEST_SHARED_LIBRARY("ffi-fixture-17"));
  eval(rt, source);
  size_t peak_bindings = 0;
  for (unsigned i = 0; i < 300u; i++) {
    snprintf(source, sizeof(source),
             "(def lib (otium.ffi/open-library '(\"%s\"))) "
             "(def current (otium.ffi/bind lib \"echo_int\" '(int) 'int #t)) "
             "(+ (saved 20) (current 22))",
             TEST_SHARED_LIBRARY("ffi-fixture-17"));
    check_int(rt, source, 42);
    ot_stats stats = ot_get_stats(rt);
    CHECK(stats.libraries == 1 && stats.bindings <= 3 && stats.signatures == 1);
    if (stats.bindings > peak_bindings) peak_bindings = stats.bindings;
  }
  eval(rt, "(set! current nil)");
  static const char* types[] = {"i8",  "u8",  "i16",   "u16",    "i32",  "u32",
                                "i64", "u64", "float", "double", "bool", "pointer"};
  for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
    snprintf(source, sizeof(source), "(otium.ffi/bind lib \"echo_%s\" '(%s) '%s #t)", types[i],
             types[i], types[i]);
    eval(rt, source);
  }
  eval(rt, "nil");
  ot_stats stats = ot_get_stats(rt);
  CHECK(stats.libraries == 1 && stats.bindings == 1 && stats.signatures <= 5);
  check_int(rt, "(saved 42)", 42);
  check_int(rt,
            "(let ((i 0)) (while (< i 2000) (let ((p (otium.ffi/alloc 'i32 1))) (otium.ffi/set! p "
            "'i32 0 i) "
            "(otium.ffi/free! p)) (set! i (+ i 1))) i)",
            2000);
  stats = ot_get_stats(rt);
  CHECK(stats.allocations == 0 && stats.bytes == 0 && stats.memory_records <= 2);

  /* A view keeps its record after the owner is freed, and reports the release. */
  eval(rt, "(def owner (otium.ffi/alloc 'i32 2)) (def view (otium.ffi/view owner 4 4)) "
           "(otium.ffi/free! owner) (set! "
           "owner nil)");
  eval(rt, "nil");
  CHECK(ot_get_stats(rt).memory_records == 1);
  ot_value result;
  const char* released = "(otium.ffi/ref view 'i32 0)";
  CHECK(!ot_eval(rt, released, strlen(released), &result));
  CHECK(strstr(ot_error(rt), "released"));
  eval(rt, "(set! view nil)");
  eval(rt, "nil");
  CHECK(ot_get_stats(rt).memory_records == 0);

  /* Memory retained by the library survives losing the last reference. */
  eval(rt, "(def retained (otium.ffi/alloc-utf8 \"persistent\")) "
           "((otium.ffi/bind lib \"retain\" '(pointer) 'void #t) retained) (set! retained nil)");
  eval(rt, "nil");
  stats = ot_get_stats(rt);
  CHECK(stats.allocations == 1 && stats.bytes == 11 && stats.memory_records == 1);
  check_true(rt, "(equal? ((otium.ffi/bind lib \"retained_text\" '() 'utf8 #t)) \"persistent\")");

  /* A binding keeps its library alive. */
  eval(rt, "(set! lib nil)");
  check_int(rt, "(saved 42)", 42);
  eval(rt, "(def holder [saved]) (set! saved nil)");
  check_int(rt, "((get holder 0) 42)", 42);
  CHECK(ot_get_stats(rt).bindings == 1);
  eval(rt, "(set! holder nil)");
  eval(rt, "nil");
  stats = ot_get_stats(rt);
  CHECK(stats.bindings == 0 && stats.signatures <= 4 && stats.libraries == 1);
  printf("peak bindings=%zu cached signatures=%zu libraries=%zu\n", peak_bindings, stats.signatures,
         stats.libraries);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(reload_releases_bindings_and_memory),
};

TEST_SUITE(foreign_reload, tests);
