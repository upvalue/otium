#include "runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static Value eval(Runtime* rt, const char* source) {
  Value value;
  if (!runtime_eval(rt, source, strlen(source), &value)) {
    fprintf(stderr, "%s\n%s\n", source, runtime_error(rt));
    abort();
  }
  return value;
}

int main(int argc, char** argv) {
  bool sanitize = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--sanitize")) sanitize = true;
    else abort();
  }
  Runtime* rt = runtime_create(16384, 4096);
  assert(rt);

  runtime_gc_stress(rt, true);
  char source[1024];
  const char* suffix = sanitize ? "_sanitize" : "";
  snprintf(source, sizeof(source),
           "(def lib (ffi/open-library '(\"./ffi_fixture%s.so\"))) "
           "(def saved (ffi/bind lib \"echo_int\" '(int) 'int #t))",
           suffix);
  eval(rt, source);
  size_t peak_bindings = 0;
  for (unsigned i = 0; i < 300u; i++) {
    snprintf(source, sizeof(source),
             "(def lib (ffi/open-library '(\"./ffi_fixture%s.so\"))) "
             "(def current (ffi/bind lib \"echo_int\" '(int) 'int #t)) "
             "(+ (saved 20) (current 22))",
             suffix);
    assert(eval(rt, source) == value_int(42));
    RuntimeForeignStats stats = runtime_foreign_stats(rt);
    assert(stats.libraries == 1 && stats.bindings <= 3 && stats.signatures == 1);
    if (stats.bindings > peak_bindings) peak_bindings = stats.bindings;
  }
  eval(rt, "(set! current nil)");
  static const char* types[] = {"i8",  "u8",  "i16",   "u16",    "i32",  "u32",
                                "i64", "u64", "float", "double", "bool", "pointer"};
  for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
    snprintf(source, sizeof(source), "(ffi/bind lib \"echo_%s\" '(%s) '%s #t)", types[i], types[i],
             types[i]);
    eval(rt, source);
  }
  eval(rt, "nil");
  RuntimeForeignStats stats = runtime_foreign_stats(rt);
  assert(stats.libraries == 1 && stats.bindings == 1 && stats.signatures <= 5);
  assert(eval(rt, "(saved 42)") == value_int(42));
  assert(eval(rt, "(let ((i 0)) (while (< i 2000) (let ((p (ffi/alloc 'i32 1))) (ffi/set! p 'i32 0 "
                  "i) (ffi/free! p)) (set! i (+ i 1))) i)") == value_int(2000));
  stats = runtime_foreign_stats(rt);
  assert(stats.allocations == 0 && stats.bytes == 0 && stats.memory_records <= 2);
  eval(rt, "(def owner (ffi/alloc 'i32 2)) (def view (ffi/view owner 4 4)) (ffi/free! owner) (set! "
           "owner nil)");
  eval(rt, "nil");
  assert(runtime_foreign_stats(rt).memory_records == 1);
  Value result;
  const char* released = "(ffi/ref view 'i32 0)";
  assert(!runtime_eval(rt, released, strlen(released), &result));
  assert(strstr(runtime_error(rt), "released"));
  eval(rt, "(set! view nil)");
  eval(rt, "nil");
  assert(runtime_foreign_stats(rt).memory_records == 0);
  eval(rt, "(def retained (ffi/alloc-utf8 \"persistent\")) "
           "((ffi/bind lib \"retain\" '(pointer) 'void #t) retained) (set! retained nil)");
  eval(rt, "nil");
  stats = runtime_foreign_stats(rt);
  assert(stats.allocations == 1 && stats.bytes == 11 && stats.memory_records == 1);
  assert(eval(rt, "(equal? ((ffi/bind lib \"retained_text\" '() 'utf8 #t)) \"persistent\")") ==
         V_TRUE);
  eval(rt, "(set! lib nil)");
  assert(eval(rt, "(saved 42)") == value_int(42));
  eval(rt, "(def holder [saved]) (set! saved nil)");
  assert(eval(rt, "((get holder 0) 42)") == value_int(42));
  assert(runtime_foreign_stats(rt).bindings == 1);
  eval(rt, "(set! holder nil)");
  eval(rt, "nil");
  stats = runtime_foreign_stats(rt);
  assert(stats.bindings == 0 && stats.signatures <= 4 && stats.libraries == 1);
  printf("foreign reload tests passed: peak bindings=%zu cached signatures=%zu libraries=%zu\n",
         peak_bindings, stats.signatures, stats.libraries);
  runtime_destroy(rt);
}
