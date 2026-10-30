/* Callback boundary cases that end the process, driven by
 * test_foreign_control.py. Usage: foreign-control outside|thread|exit
 *   outside  calls a stored callback after the foreign call returned (aborts)
 *   thread   calls a callback from another thread (aborts)
 *   exit     exits from a callback and checks the outer cleanup ran */
#define _POSIX_C_SOURCE 200809L
#include "helpers.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s outside|thread|exit\n", argv[0]);
    return 2;
  }
  const char* mode = argv[1];
  ot_state* rt = new_runtime(128 * 1024, 4096);
  char source[2048];
  snprintf(source, sizeof(source),
           "(def lib (otium.ffi/open-library '(\"/missing/ffi/library\" \"%s\"))) "
           "(def other (otium.ffi/open-library '(\"%s\"))) "
           "(def extra (otium.ffi/open-library '(\"%s\")))",
           TEST_SHARED_LIBRARY("ffi-fixture-17"), TEST_SHARED_LIBRARY("ffi-fixture-29"),
           TEST_SHARED_LIBRARY("c/ffi-extra"));
  eval(rt, source);
  ot_gc_stress(rt, true);
  if (!strcmp(mode, "exit")) {
    CHECK(ot_load_core(rt, TEST_LIBRARY_DIR));
    eval(rt, "(def cleaned 0)");
    const char* program =
        "(defer ((otium.ffi/bind lib \"call_n\" '(callback-pointer int) 'int #t)"
        " (otium.ffi/%callback '(12) 12 (fn (x) (exit 3))) 2) (set! cleaned (+ cleaned 1)))";
    ot_value result;
    CHECK(!ot_eval(rt, program, strlen(program), &result));
    int status;
    CHECK(ot_exit_status(rt, &status) && status == 3);
    check_int(rt, "cleaned", 1);
    ot_destroy(rt);
    puts("callback exit and outer cleanup passed");
    return 0;
  }
  if (!strcmp(mode, "thread")) {
    eval(rt, "((otium.ffi/bind lib \"call_on_thread\" '(callback-pointer) 'void #t)"
             " (otium.ffi/%callback '() 0 (fn () nil)))");
    abort();
  }
  if (strcmp(mode, "outside")) {
    fprintf(stderr, "unknown mode: %s\n", mode);
    return 2;
  }
  eval(rt, "((otium.ffi/bind lib \"store_callback\" '(callback-pointer) 'void #t)"
           " (otium.ffi/%callback '(12) 12 (fn (x) x)))");
  void* library = dlopen(TEST_SHARED_LIBRARY("ffi-fixture-17"), RTLD_NOW | RTLD_LOCAL);
  CHECK(library);
  void* symbol = dlsym(library, "call_stored");
  int (*invoke)(int);
  memcpy(&invoke, &symbol, sizeof(invoke));
  CHECK(invoke);
  invoke(1);
  abort();
}
