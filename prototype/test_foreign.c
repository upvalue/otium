#define _POSIX_C_SOURCE 200809L
#include "runtime.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Value eval(Runtime* rt, const char* source) {
  Value v;
  if (!runtime_eval(rt, source, strlen(source), &v)) {
    fprintf(stderr, "%s\n%s\n", source, runtime_error(rt));
    abort();
  }
  return v;
}

static void eval_file(Runtime* rt, const char* path) {
  FILE* file = fopen(path, "rb");
  assert(file);
  assert(!fseek(file, 0, SEEK_END));
  long length = ftell(file);
  assert(length >= 0);
  rewind(file);
  char* contents = malloc((size_t)length + 1);
  assert(contents);
  assert(fread(contents, 1, (size_t)length, file) == (size_t)length);
  fclose(file);
  contents[length] = 0;
  eval(rt, contents);
  free(contents);
}

static void integer(Runtime* rt, const char* source, intptr_t expected) {
  Value actual = eval(rt, source);
  if (actual != value_int(expected)) {
    fprintf(stderr, "%s: expected %ld, got ", source, (long)expected);
    runtime_print(rt, stderr, actual);
    fputc('\n', stderr);
    abort();
  }
}

static void string(Runtime* rt, const char* source, const char* expected) {
  Value v = eval(rt, source);
  FILE* file = tmpfile();
  assert(file);
  runtime_print(rt, file, v);
  long n = ftell(file);
  assert(n >= 0);
  rewind(file);
  char* bytes = malloc((size_t)n + 1);
  assert(bytes);
  assert(fread(bytes, 1, (size_t)n, file) == (size_t)n);
  bytes[n] = 0;
  assert(!strcmp(bytes, expected));
  free(bytes);
  fclose(file);
}

static void rejects(Runtime* rt, const char* source, const char* message) {
  Value result;
  if (runtime_eval(rt, source, strlen(source), &result) || !strstr(runtime_error(rt), message)) {
    fprintf(stderr, "%s: expected %s, got %s\n", source, message, runtime_error(rt));
    abort();
  }
  integer(rt, "(+ 20 22)", 42);
}

int main(int argc, char** argv) {
  bool sanitize = false;
  const char* control = NULL;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--sanitize")) sanitize = true;
    else if (!strncmp(argv[i], "--callback-", 11)) control = argv[i] + 11;
    else abort();
  }
  Runtime* rt = runtime_create(128 * 1024, 4096);
  assert(rt);

  char source[2048];
  const char* suffix = sanitize ? "_sanitize" : "";
  snprintf(source, sizeof(source),
           "(def lib (ffi/open-library '(\"/missing/ffi/library\" \"./ffi_fixture%s.so\"))) "
           "(def other (ffi/open-library '(\"./ffi_other%s.so\"))) "
           "(def extra (ffi/open-library '(\"./ffi_extra%s.so\")))",
           suffix, suffix, suffix);
  eval(rt, source);
  if (control) {
    runtime_gc_stress(rt, true);
    if (!strcmp(control, "exit")) {
      assert(runtime_load_core(rt, "../lib"));
      eval(rt, "(def cleaned 0)");
      const char* program =
          "(defer ((ffi/bind lib \"call_n\" '(callback-pointer int) 'int #t)"
          " (ffi/%callback '(12) 12 (fn (x) (exit 3))) 2) (set! cleaned (+ cleaned 1)))";
      Value result;
      assert(!runtime_eval(rt, program, strlen(program), &result));
      int status;
      assert(runtime_exit_status(rt, &status) && status == 3);
      integer(rt, "cleaned", 1);
      runtime_destroy(rt);
      puts("callback exit and outer cleanup passed");
      return 0;
    }
    if (!strcmp(control, "thread")) {
      eval(rt, "((ffi/bind lib \"call_on_thread\" '(callback-pointer) 'void #t)"
               " (ffi/%callback '() 0 (fn () nil)))");
      abort();
    }
    assert(!strcmp(control, "outside"));
    eval(rt, "((ffi/bind lib \"store_callback\" '(callback-pointer) 'void #t)"
             " (ffi/%callback '(12) 12 (fn (x) x)))");
    snprintf(source, sizeof(source), "./ffi_fixture%s.so", suffix);
    void* library = dlopen(source, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    void* symbol = dlsym(library, "call_stored");
    int (*invoke)(int);
    memcpy(&invoke, &symbol, sizeof(invoke));
    assert(invoke);
    invoke(1);
    abort();
  }
  integer(rt, "((ffi/bind lib \"fixture_id\" '() 'int #t))", 17);
  assert(runtime_foreign_signatures(rt) == 1);
  integer(rt, "((ffi/bind other \"fixture_id\" '() 'int #f))", 29);
  assert(runtime_foreign_signatures(rt) == 1);
  integer(rt, "(apply (ffi/bind lib \"fixture_id\" '() 'int #t) nil)", 17);

  assert(eval(rt, "(ffi/has-symbol? lib \"echo_bool\")") == V_TRUE);
  assert(eval(rt, "(ffi/has-symbol? lib \"absent\")") == V_FALSE);
  rejects(rt, "(ffi/open-library '(\"/missing/ffi/library\"))", "load");
  rejects(rt, "(ffi/bind lib \"absent\" '() 'int #t)", "symbol");
  rejects(rt, "(ffi/bind lib \"fixture_id\" '() 'int 0)", "boolean");
  rejects(rt, "(ffi/bind lib \"fixture_id\" '() 'int)", "arity");
  rejects(rt, "(ffi/bind lib \"fixture_id\" '(void) 'int #t)", "void argument");
  rejects(rt, "(ffi/bind lib \"fixture_id\" '(unknown) 'int #t)", "unsupported scalar");
  rejects(rt, "(ffi/bind lib \"fixture_id\" '(int . int) 'int #t)", "improper");
  rejects(rt, "(let ((p (list 'int))) (set-cdr! p p) (ffi/bind lib \"fixture_id\" p 'int #t))",
          "cyclic");

  static const struct {
    const char *type, *symbol;
    intptr_t value;
  } integers[] = {{"i8", "i8", -128},
                  {"u8", "u8", 255},
                  {"i16", "i16", -32768},
                  {"u16", "u16", 65535},
                  {"i32", "i32", -2147483647},
                  {"u32", "u32", 4294967295},
                  {"i64", "i64", -4611686018427387904},
                  {"u64", "u64", 4611686018427387903},
                  {"short", "short", -1234},
                  {"ushort", "ushort", 54321},
                  {"int", "int", -1234567},
                  {"uint", "uint", 1234567},
                  {"long", "long", -1234567},
                  {"ulong", "ulong", 1234567},
                  {"size-t", "size_t", 1234567},
                  {"ptrdiff-t", "ptrdiff_t", -1234567}};

  for (size_t i = 0; i < sizeof(integers) / sizeof(integers[0]); i++) {
    snprintf(source, sizeof(source), "((ffi/bind lib \"echo_%s\" '(%s) '%s #t) %ld)",
             integers[i].symbol, integers[i].type, integers[i].type, (long)integers[i].value);
    integer(rt, source, integers[i].value);
    snprintf(source, sizeof(source),
             "(let ((p (ffi/alloc '%s 1))) (ffi/set! p '%s 0 %ld) "
             "(let ((v (ffi/ref p '%s 0))) (ffi/free! p) v))",
             integers[i].type, integers[i].type, (long)integers[i].value, integers[i].type);
    integer(rt, source, integers[i].value);
  }
  eval(rt, "(def byte (ffi/bind lib \"echo_u8\" '(u8) 'u8 #t))"
           "(def boolean (ffi/bind lib \"echo_bool\" '(bool) 'bool #t))"
           "(def single (ffi/bind lib \"echo_float\" '(float) 'float #t))"
           "(def real (ffi/bind lib \"echo_double\" '(double) 'double #f))"
           "(def text (ffi/bind lib \"echo_utf8\" '(utf8) 'utf8 #t))"
           "(def pointer (ffi/bind lib \"echo_pointer\" '(pointer) 'pointer #t))");
  assert(eval(rt, "(boolean #t)") == V_TRUE);
  assert(eval(rt, "(boolean #f)") == V_FALSE);
  rejects(rt, "(boolean 1)", "boolean");
  rejects(rt, "(boolean)", "arity");
  rejects(rt, "(byte 256)", "range");
  rejects(rt, "(byte -1)", "range");
  rejects(rt, "(byte 1.0)", "exact integer");
  assert(eval(rt, "(= (single 16777217) 16777216.0)") == V_TRUE);
  assert(eval(rt, "(= (real 1.25) 1.25)") == V_TRUE);
  rejects(rt, "(single 1e100)", "range");
  rejects(rt, "((ffi/bind lib \"high_u64\" '() 'u64 #t))", "range");
  integer(rt, "((ffi/bind lib \"calls\" '() 'int #t))", 1);
  rejects(rt, "((ffi/bind extra \"lowest_integer\" '() 'i64 #t))", "range");
  string(rt, "(text \"héllo 🌍\")", "héllo 🌍");
  string(rt, "(text \"\")", "");
  assert(eval(rt, "(ffi/null? (text ffi/null))") == V_TRUE);
  rejects(rt, "(text #f)", "string");
  rejects(rt, "(text \"a\\0b\")", "embedded NUL");
  rejects(rt, "((ffi/bind lib \"bad_utf8\" '() 'utf8 #t))", "encoding");
  eval(rt, "(def first ((ffi/bind lib \"changing_text\" '() 'utf8 #t)))");
  assert(eval(rt, "((ffi/bind lib \"change_text\" '() 'void #t))") == V_FALSE);
  string(rt, "first", "first");
  string(rt, "((ffi/bind lib \"changing_text\" '() 'utf8 #t))", "second");
  string(rt, "((ffi/bind extra \"offset_text\" '(utf8) 'utf8 #t) \"hello\")", "ello");
  rejects(rt, "((ffi/bind extra \"offset_text\" '(utf8) 'pointer #t) \"hello\")",
          "temporary UTF-8");
  assert(eval(rt, "(ffi/null? ffi/null)") == V_TRUE);
  assert(eval(rt, "(ffi/null? 0)") == V_FALSE);
  assert(eval(rt, "(ffi/pointer? ffi/null)") == V_TRUE);
  assert(eval(rt, "(if ffi/null #t #f)") == V_TRUE);
  assert(eval(rt, "(ffi/null? (pointer ffi/null))") == V_TRUE);
  rejects(rt, "(pointer 0)", "pointer");
  assert(eval(rt, "((ffi/bind lib \"null_callback\" '(callback-pointer) 'bool #t) ffi/null)") ==
         V_TRUE);

  eval(rt, "(def p (ffi/alloc 'u64 2)) (def view (ffi/view p 8 8))");
  integer(rt, "(ffi/allocation-count)", 1);
  integer(rt, "(ffi/allocation-bytes)", 16);
  integer(rt, "(ffi/ref p 'u64 0)", 0);
  eval(rt, "(ffi/set! view 'u64 0 1234)");
  integer(rt, "(ffi/ref p 'u64 1)", 1234);
  rejects(rt, "(ffi/ref p 'u64 2)", "bounds");
  rejects(rt, "(ffi/ref (ffi/view p 1 8) 'u64 0)", "alignment");
  rejects(rt, "(ffi/view view 0 9)", "bounds");
  rejects(rt, "(ffi/free! view)", "owning");
  eval(rt, "(def alias (pointer p))");
  assert(eval(rt, "(ffi/pointer=? p alias)") == V_TRUE);
  eval(rt, "(ffi/free! p)");
  assert(eval(rt, "(ffi/free! p)") == V_FALSE);
  rejects(rt, "(ffi/ref view 'u64 0)", "released");
  rejects(rt, "(pointer p)", "released");
  assert(eval(rt, "(ffi/pointer=? p alias)") == V_TRUE);
  rejects(rt, "(ffi/free! alias)", "owning");
  integer(rt, "(ffi/allocation-count)", 0);
  integer(rt, "(ffi/allocation-bytes)", 0);
  eval(rt, "(def zero (ffi/alloc 'u8 0))");
  rejects(rt, "(ffi/ref zero 'u8 0)", "bounds");
  eval(rt, "(ffi/free! zero)");
  eval(rt, "(def storage (ffi/alloc-utf8 \"retained λ\"))"
           "((ffi/bind lib \"retain\" '(pointer) 'void #t) storage)");
  runtime_gc_stress(rt, true);
  string(rt, "((ffi/bind lib \"retained_text\" '() 'utf8 #t))", "retained λ");
  string(rt, "(ffi/read-utf8 storage 12)", "retained λ");
  rejects(rt, "(ffi/read-utf8 storage 2)", "bounds");
  string(rt, "(text (text \"moves across GC\"))", "moves across GC");
  assert(eval(rt, "(= (real (single 1.25)) 1.25)") == V_TRUE);
  eval(rt, "(def interior ((ffi/bind extra \"offset_pointer\" '(pointer) 'pointer #t) storage))");
  string(rt, "(ffi/read-utf8 interior 11)", "etained λ");
  eval(rt, "(ffi/free! storage)");
  rejects(rt, "(ffi/free! interior)", "owning");
  eval(rt, "(def slots (ffi/alloc 'pointer 1))"
           "((ffi/bind lib \"pointer_out\" '(pointer pointer) 'void #t) slots slots)");
  assert(eval(rt, "(ffi/pointer=? slots (ffi/ref slots 'pointer 0))") == V_TRUE);
  eval(rt, "(ffi/free! slots)");
  eval(rt, "(def mix (ffi/bind extra \"abi_mix\" "
           "'(int double int double int double int double int double int double int double int "
           "double pointer bool float) 'double #t))");
  assert(
      eval(rt,
           "(= (mix 1 0.5 2 1.5 3 2.5 4 3.5 5 4.5 6 5.5 7 6.5 8 7.5 ffi/null #t 0.25) 168.25)") ==
      V_TRUE);
  eval(rt, "(def (tail-call x) (byte x))");
  integer(rt, "(tail-call 42)", 42);
  Value argument = value_int(43), result;
  assert(runtime_call(rt, "byte", &argument, 1, &result) && result == argument);
  integer(rt, "(ffi/allocation-count)", 0);
  assert(runtime_collections(rt) > 10);
  runtime_destroy(rt);
  rt = runtime_create(8192, 256);
  assert(rt);

  snprintf(source, sizeof(source),
           "(def lib (ffi/open-library '(\"./ffi_fixture%s.so\"))) "
           "(def text (ffi/bind lib \"echo_utf8\" '(utf8) 'utf8 #t))",
           suffix);
  eval(rt, source);
  string(rt,
         "(let loop ((n 10000) (value (text \"retained\"))) "
         "(if (= n 0) value (loop (- n 1) (text value))))",
         "retained");
  assert(runtime_collections(rt) > 10);
  runtime_destroy(rt);
  rt = runtime_create(128, 64);
  assert(rt);

  const char* exhaustion = "(ffi/alloc 'u8 5)";
  assert(!runtime_eval(rt, exhaustion, strlen(exhaustion), &result));
  assert(strstr(runtime_error(rt), "heap exhausted"));
  assert(runtime_call(rt, "ffi/allocation-count", NULL, 0, &result) && result == value_int(0));
  assert(runtime_call(rt, "ffi/allocation-bytes", NULL, 0, &result) && result == value_int(0));
  runtime_destroy(rt);
  rt = runtime_create(4 * 1024 * 1024, 256 * 1024);
  assert(rt);
  assert(runtime_load_core(rt, "../lib"));
  runtime_gc_stress(rt, true);
  FILE* bulk = fopen("../tests/ffi/bulk.ot", "rb");
  assert(bulk);
  assert(!fseek(bulk, 0, SEEK_END));
  long length = ftell(bulk);
  assert(length >= 0);
  rewind(bulk);
  char* contents = malloc((size_t)length + 1);
  assert(contents);
  assert(fread(contents, 1, (size_t)length, bulk) == (size_t)length);
  fclose(bulk);
  contents[length] = 0;
  eval(rt, contents);
  free(contents);
  eval(rt, "(def overlap (ffi/alloc 'u8 6)) (ffi/write-bytes! overlap #u8(1 2 3 4 5 6))"
           "(ffi/copy! (ffi/view overlap 1 5) overlap 5)");
  string(rt, "(str (ffi/read-bytes overlap 6))", "#u8(1 1 2 3 4 5)");
  eval(rt, "(ffi/copy! overlap (ffi/view overlap 1 5) 5)");
  string(rt, "(str (ffi/read-bytes overlap 6))", "#u8(1 2 3 4 5 5)");
  eval(rt, "(def detached (ffi/read-bytes overlap 6)) (ffi/write-bytes! overlap #u8(9))");
  integer(rt, "(bytevector-u8-ref detached 0)", 1);
  rejects(rt, "(ffi/copy! overlap (ffi/view overlap 0 2) 3)", "bounds");
  rejects(rt, "(ffi/copy! (ffi/view overlap 0 2) overlap 3)", "bounds");
  eval(rt, "(ffi/free! overlap)");
  rejects(rt, "(ffi/copy! overlap overlap 0)", "released");
  eval(rt, "(def spec (list 'array 'double 2))"
           "(def fields (list (list 'tag 'u8) (list 'values spec)))"
           "(def Layout (ffi/define-struct! 'Layout fields))"
           "(set-car! (cdr spec) 'u8) (set-car! (car fields) 'changed)");
  integer(rt, "(ffi/sizeof Layout)", 24);
  integer(rt, "(ffi/alignof Layout)", 8);
  integer(rt, "(ffi/offset-of Layout '(values 1))", 16);
  eval(rt,
       "(def saved-layout Layout)"
       "(def Container (ffi/define-struct! 'Container (list (list 'items (list 'array Layout 2)))))"
       "(set! Layout (ffi/define-struct! 'Layout '((tag u8))))"
       "(def aggregate (ffi/alloc Container 1))"
       "(ffi/field-set! aggregate Container '(items 1 values 1) 3.5)");
  integer(rt, "(ffi/sizeof Container)", 48);
  assert(eval(rt, "(= (ffi/field-ref aggregate Container '(items 1 values 1)) 3.5)") == V_TRUE);
  eval(rt, "(def aggregate-view (ffi/field-view aggregate Container '(items 1)))");
  assert(eval(rt, "(= (ffi/field-ref aggregate-view saved-layout '(values 1)) 3.5)") == V_TRUE);
  eval(rt, "(def Union (ffi/define-union! 'Union '((tag u8) (value double))))");
  integer(rt, "(ffi/sizeof Union)", 8);
  integer(rt, "(ffi/offset-of Union '(value))", 0);
  eval(rt, "(ffi/free! aggregate)");
  rejects(rt, "(ffi/field-ref aggregate-view saved-layout '(tag))", "released");
  rejects(rt, "(ffi/define-struct! 'Duplicate '((a u8) (a double)))", "duplicate");
  rejects(rt, "(ffi/offset-of Container '(items 2))", "bounds");
  integer(rt, "(ffi/allocation-count)", 0);
  runtime_destroy(rt);
  rt = runtime_create(4 * 1024 * 1024, 256 * 1024);
  assert(rt);
  assert(runtime_load_core(rt, "../lib"));
  runtime_gc_stress(rt, true);
  snprintf(source, sizeof(source), "(def aggregate-fixture \"./ffi_fixture%s.so\")", suffix);
  eval(rt, source);
  FILE* aggregates = fopen("test_aggregate.ot", "rb");
  assert(aggregates);
  assert(!fseek(aggregates, 0, SEEK_END));
  length = ftell(aggregates);
  assert(length >= 0);
  rewind(aggregates);
  contents = malloc((size_t)length + 1);
  assert(contents);
  assert(fread(contents, 1, (size_t)length, aggregates) == (size_t)length);
  fclose(aggregates);
  contents[length] = 0;
  eval(rt, contents);
  free(contents);
  size_t prepared = runtime_foreign_signatures(rt);
  eval(rt, "(def SamePoint (ffi/define-struct! 'OtherName '((a double) (b double))))"
           "(def same-translate (ffi/bind aggregate-lib \"translate\" (list SamePoint 'double "
           "'double) SamePoint #t))");
  assert(runtime_foreign_signatures(rt) == prepared);
  /* Only prepare these signatures: the fixture has a different C type. */
  eval(rt, "(def DifferentPoint (ffi/define-struct! 'Point '((a u64) (b double))))"
           "(def unused-binding (ffi/bind aggregate-lib \"translate\" (list DifferentPoint 'double "
           "'double) DifferentPoint #t))");
  assert(runtime_foreign_signatures(rt) == prepared + 1);
  eval(rt, "(dotimes (i 40)"
           " (let ((layout (ffi/define-struct! 'Temporary (list (list 'data (list 'array 'u8 (+ i "
           "1)))))))"
           "   (ffi/bind aggregate-lib \"translate\" (list layout) layout #t)))");
  eval(rt, "(+ 20 22)");
  assert(runtime_foreign_signatures(rt) <= prepared + 6);
  runtime_destroy(rt);
  rt = runtime_create(4 * 1024 * 1024, 256 * 1024);
  assert(rt);
  assert(runtime_load_core(rt, "../lib"));
  char fixture[128], other[128];
  snprintf(fixture, sizeof(fixture), "./ffi_fixture%s.so", suffix);
  snprintf(other, sizeof(other), "./ffi_other%s.so", suffix);
  const char* arguments[] = {fixture, other};
  assert(runtime_set_arguments(rt, "../tests/ffi/common.ot", 2, arguments));
  runtime_gc_stress(rt, true);
  eval_file(rt, "../tests/ffi/support.ot");
  eval_file(rt, "../tests/ffi/runtime-c.ot");
  eval_file(rt, "../tests/ffi/common.ot");
  integer(rt, "(ffi/allocation-count)", 0);
  runtime_destroy(rt);
  rt = runtime_create(4 * 1024 * 1024, 256 * 1024);
  assert(rt);
  assert(runtime_load_core(rt, "../lib"));
  char directory[] = "/tmp/otium-callback-test-XXXXXX";
  assert(mkdtemp(directory));
  const char* callback_arguments[] = {fixture, other, directory};
  assert(runtime_set_arguments(rt, "../tests/ffi/callbacks.ot", 3, callback_arguments));
  assert(runtime_load_path(rt, directory));
  runtime_gc_stress(rt, true);
  eval_file(rt, "../tests/ffi/support.ot");
  eval_file(rt, "../tests/ffi/runtime-c.ot");
  eval_file(rt, "../tests/ffi/callbacks.ot");
  snprintf(source, sizeof(source),
           "(def callback-extra (ffi/open-library '(\"./ffi_extra%s.so\")))", suffix);
  eval(rt, source);
  eval_file(rt, "test_callback_lifetime.ot");
  size_t callback_records = runtime_foreign_stats(rt).callback_records;
  for (size_t i = 0; i < 100; i++) {
    eval(rt, "(def temporary-callback (ffi/callback '(int) 'int (fn (x) (+ x 1))))"
             "(ffi/free-callback! temporary-callback) (set! temporary-callback nil)");
    assert(runtime_foreign_stats(rt).callback_records <= callback_records + 2);
  }
  eval(rt, "(+ 20 22)");
  assert(runtime_foreign_stats(rt).callback_records <= callback_records);
  assert(runtime_foreign_stats(rt).callbacks == 1);
  snprintf(source, sizeof(source), "%s/ffi-callback-handler.ot", directory);
  assert(!unlink(source));
  assert(!rmdir(directory));
  runtime_destroy(rt);
  rt = runtime_create(4 * 1024 * 1024, 256 * 1024);
  assert(rt);
  assert(runtime_load_core(rt, "../lib"));
  runtime_gc_stress(rt, true);
  eval_file(rt, "../tests/ffi/ownership.ot");
  eval_file(rt, "test_ownership.ot");
  runtime_destroy(rt);
  puts("foreign call tests passed");
}
