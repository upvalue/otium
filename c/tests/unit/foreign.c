#define _POSIX_C_SOURCE 200809L
#include "helpers.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FIXTURE TEST_SHARED_LIBRARY("ffi-fixture-17")
#define OTHER TEST_SHARED_LIBRARY("ffi-fixture-29")
#define EXTRA TEST_SHARED_LIBRARY("c/ffi-extra")

/* Open the fixture libraries as lib, other and extra. The first candidate for
 * lib is missing, so loading falls through to the next one. */
static void open_fixtures(ot_state* rt) {
  char source[2048];
  snprintf(source, sizeof(source),
           "(def lib (ffi/open-library '(\"/missing/ffi/library\" \"%s\"))) "
           "(def other (ffi/open-library '(\"%s\"))) "
           "(def extra (ffi/open-library '(\"%s\")))",
           FIXTURE, OTHER, EXTRA);
  eval(rt, source);
}

static ot_state* fixture_runtime(void) {
  ot_state* rt = new_runtime(128 * 1024, 4096);
  open_fixtures(rt);
  return rt;
}

static void define_scalar_bindings(ot_state* rt) {
  eval(rt, "(def byte (ffi/bind lib \"echo_u8\" '(u8) 'u8 #t))"
           "(def boolean (ffi/bind lib \"echo_bool\" '(bool) 'bool #t))"
           "(def single (ffi/bind lib \"echo_float\" '(float) 'float #t))"
           "(def real (ffi/bind lib \"echo_double\" '(double) 'double #f))"
           "(def text (ffi/bind lib \"echo_utf8\" '(utf8) 'utf8 #t))"
           "(def pointer (ffi/bind lib \"echo_pointer\" '(pointer) 'pointer #t))");
}

static ot_state* core_runtime(void) {
  ot_state* rt = new_runtime(4 * 1024 * 1024, 256 * 1024);
  CHECK(ot_load_core(rt, TEST_LIBRARY_DIR));
  return rt;
}

TEST(library_loading_and_binding) {
  ot_state* rt = fixture_runtime();
  check_int(rt, "((ffi/bind lib \"fixture_id\" '() 'int #t))", 17);
  CHECK(ot_get_stats(rt).signatures == 1);
  check_int(rt, "((ffi/bind other \"fixture_id\" '() 'int #f))", 29);
  CHECK(ot_get_stats(rt).signatures == 1);
  check_int(rt, "(apply (ffi/bind lib \"fixture_id\" '() 'int #t) nil)", 17);

  check_true(rt, "(ffi/has-symbol? lib \"echo_bool\")");
  CHECK(eval(rt, "(ffi/has-symbol? lib \"absent\")") == OT_FALSE);
  check_error(rt, "(ffi/open-library '(\"/missing/ffi/library\"))", "load");
  check_error(rt, "(ffi/bind lib \"absent\" '() 'int #t)", "symbol");
  check_error(rt, "(ffi/bind lib \"fixture_id\" '() 'int 0)", "boolean");
  check_error(rt, "(ffi/bind lib \"fixture_id\" '() 'int)", "arity");
  check_error(rt, "(ffi/bind lib \"fixture_id\" '(void) 'int #t)", "void argument");
  check_error(rt, "(ffi/bind lib \"fixture_id\" '(unknown) 'int #t)", "unsupported scalar");
  check_error(rt, "(ffi/bind lib \"fixture_id\" '(int . int) 'int #t)", "improper");
  check_error(rt, "(let ((p (list 'int))) (set-cdr! p p) (ffi/bind lib \"fixture_id\" p 'int #t))",
              "cyclic");
  ot_destroy(rt);
}

TEST(integer_types_round_trip) {
  ot_state* rt = fixture_runtime();

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

  char source[2048];
  for (size_t i = 0; i < sizeof(integers) / sizeof(integers[0]); i++) {
    snprintf(source, sizeof(source), "((ffi/bind lib \"echo_%s\" '(%s) '%s #t) %ld)",
             integers[i].symbol, integers[i].type, integers[i].type, (long)integers[i].value);
    check_int(rt, source, integers[i].value);
    snprintf(source, sizeof(source),
             "(let ((p (ffi/alloc '%s 1))) (ffi/set! p '%s 0 %ld) "
             "(let ((v (ffi/ref p '%s 0))) (ffi/free! p) v))",
             integers[i].type, integers[i].type, (long)integers[i].value, integers[i].type);
    check_int(rt, source, integers[i].value);
  }
  ot_destroy(rt);
}

TEST(scalar_text_and_pointer_conversions) {
  ot_state* rt = fixture_runtime();
  define_scalar_bindings(rt);
  check_true(rt, "(boolean #t)");
  CHECK(eval(rt, "(boolean #f)") == OT_FALSE);
  check_error(rt, "(boolean 1)", "boolean");
  check_error(rt, "(boolean)", "arity");
  check_error(rt, "(byte 256)", "range");
  check_error(rt, "(byte -1)", "range");
  check_error(rt, "(byte 1.0)", "exact integer");
  check_true(rt, "(= (single 16777217) 16777216.0)");
  check_true(rt, "(= (real 1.25) 1.25)");
  check_error(rt, "(single 1e100)", "range");
  check_error(rt, "((ffi/bind lib \"high_u64\" '() 'u64 #t))", "range");
  check_int(rt, "((ffi/bind lib \"calls\" '() 'int #t))", 1);
  check_error(rt, "((ffi/bind extra \"lowest_integer\" '() 'i64 #t))", "range");
  check_printed(rt, "(text \"héllo 🌍\")", "héllo 🌍");
  check_printed(rt, "(text \"\")", "");
  check_true(rt, "(ffi/null? (text ffi/null))");
  check_error(rt, "(text #f)", "string");
  check_error(rt, "(text \"a\\0b\")", "embedded NUL");
  check_error(rt, "((ffi/bind lib \"bad_utf8\" '() 'utf8 #t))", "encoding");

  /* Returned text is copied, so later changes in C do not reach it. */
  eval(rt, "(def first ((ffi/bind lib \"changing_text\" '() 'utf8 #t)))");
  CHECK(eval(rt, "((ffi/bind lib \"change_text\" '() 'void #t))") == OT_FALSE);
  check_printed(rt, "first", "first");
  check_printed(rt, "((ffi/bind lib \"changing_text\" '() 'utf8 #t))", "second");
  check_printed(rt, "((ffi/bind extra \"offset_text\" '(utf8) 'utf8 #t) \"hello\")", "ello");
  check_error(rt, "((ffi/bind extra \"offset_text\" '(utf8) 'pointer #t) \"hello\")",
              "temporary UTF-8");

  check_true(rt, "(ffi/null? ffi/null)");
  CHECK(eval(rt, "(ffi/null? 0)") == OT_FALSE);
  check_true(rt, "(ffi/pointer? ffi/null)");
  check_true(rt, "(if ffi/null #t #f)");
  check_true(rt, "(ffi/null? (pointer ffi/null))");
  check_error(rt, "(pointer 0)", "pointer");
  check_true(rt, "((ffi/bind lib \"null_callback\" '(callback-pointer) 'bool #t) ffi/null)");
  ot_destroy(rt);
}

/* Allocation, views and release, then the same calls with collection before
 * every allocation. */
TEST(foreign_memory_and_collection) {
  ot_state* rt = fixture_runtime();
  define_scalar_bindings(rt);
  eval(rt, "(def p (ffi/alloc 'u64 2)) (def view (ffi/view p 8 8))");
  check_int(rt, "(ffi/allocation-count)", 1);
  check_int(rt, "(ffi/allocation-bytes)", 16);
  check_int(rt, "(ffi/ref p 'u64 0)", 0);
  eval(rt, "(ffi/set! view 'u64 0 1234)");
  check_int(rt, "(ffi/ref p 'u64 1)", 1234);
  check_error(rt, "(ffi/ref p 'u64 2)", "bounds");
  check_error(rt, "(ffi/ref (ffi/view p 1 8) 'u64 0)", "alignment");
  check_error(rt, "(ffi/view view 0 9)", "bounds");
  check_error(rt, "(ffi/free! view)", "owning");
  eval(rt, "(def alias (pointer p))");
  check_true(rt, "(ffi/pointer=? p alias)");
  eval(rt, "(ffi/free! p)");
  CHECK(eval(rt, "(ffi/free! p)") == OT_FALSE);
  check_error(rt, "(ffi/ref view 'u64 0)", "released");
  check_error(rt, "(pointer p)", "released");
  check_true(rt, "(ffi/pointer=? p alias)");
  check_error(rt, "(ffi/free! alias)", "owning");
  check_int(rt, "(ffi/allocation-count)", 0);
  check_int(rt, "(ffi/allocation-bytes)", 0);
  eval(rt, "(def zero (ffi/alloc 'u8 0))");
  check_error(rt, "(ffi/ref zero 'u8 0)", "bounds");
  eval(rt, "(ffi/free! zero)");
  eval(rt, "(def storage (ffi/alloc-utf8 \"retained λ\"))"
           "((ffi/bind lib \"retain\" '(pointer) 'void #t) storage)");

  ot_gc_stress(rt, true);
  check_printed(rt, "((ffi/bind lib \"retained_text\" '() 'utf8 #t))", "retained λ");
  check_printed(rt, "(ffi/read-utf8 storage 12)", "retained λ");
  check_error(rt, "(ffi/read-utf8 storage 2)", "bounds");
  check_printed(rt, "(text (text \"moves across GC\"))", "moves across GC");
  check_true(rt, "(= (real (single 1.25)) 1.25)");
  eval(rt, "(def interior ((ffi/bind extra \"offset_pointer\" '(pointer) 'pointer #t) storage))");
  check_printed(rt, "(ffi/read-utf8 interior 11)", "etained λ");
  eval(rt, "(ffi/free! storage)");
  check_error(rt, "(ffi/free! interior)", "owning");
  eval(rt, "(def slots (ffi/alloc 'pointer 1))"
           "((ffi/bind lib \"pointer_out\" '(pointer pointer) 'void #t) slots slots)");
  check_true(rt, "(ffi/pointer=? slots (ffi/ref slots 'pointer 0))");
  eval(rt, "(ffi/free! slots)");
  eval(rt, "(def mix (ffi/bind extra \"abi_mix\" "
           "'(int double int double int double int double int double int double int double int "
           "double pointer bool float) 'double #t))");
  check_true(rt,
             "(= (mix 1 0.5 2 1.5 3 2.5 4 3.5 5 4.5 6 5.5 7 6.5 8 7.5 ffi/null #t 0.25) 168.25)");
  eval(rt, "(def (tail-call x) (byte x))");
  check_int(rt, "(tail-call 42)", 42);
  ot_value argument = ot_int(43), result;
  CHECK(ot_call(rt, "byte", &argument, 1, &result) && result == argument);
  check_int(rt, "(ffi/allocation-count)", 0);
  CHECK(ot_get_stats(rt).collections > 10);
  ot_destroy(rt);
}

TEST(text_survives_collection_in_small_heap) {
  ot_state* rt = new_runtime(8192, 256);
  char source[2048];
  snprintf(source, sizeof(source),
           "(def lib (ffi/open-library '(\"%s\"))) "
           "(def text (ffi/bind lib \"echo_utf8\" '(utf8) 'utf8 #t))",
           FIXTURE);
  eval(rt, source);
  check_printed(rt,
                "(let loop ((n 10000) (value (text \"retained\"))) "
                "(if (= n 0) value (loop (- n 1) (text value))))",
                "retained");
  CHECK(ot_get_stats(rt).collections > 10);
  ot_destroy(rt);
}

TEST(allocation_reports_heap_exhaustion) {
  ot_state* rt = new_runtime(128, 64);
  ot_value result;
  const char* exhaustion = "(ffi/alloc 'u8 5)";
  CHECK(!ot_eval(rt, exhaustion, strlen(exhaustion), &result));
  CHECK(strstr(ot_error(rt), "heap exhausted"));
  CHECK(ot_call(rt, "ffi/allocation-count", NULL, 0, &result) && result == ot_int(0));
  CHECK(ot_call(rt, "ffi/allocation-bytes", NULL, 0, &result) && result == ot_int(0));
  ot_destroy(rt);
}

TEST(bulk_memory_and_layouts) {
  ot_state* rt = core_runtime();
  ot_gc_stress(rt, true);
  eval_file(rt, "tests/ffi/bulk.ot");

  /* Overlapping copies behave like memmove. */
  eval(rt, "(def overlap (ffi/alloc 'u8 6)) (ffi/write-bytes! overlap #u8(1 2 3 4 5 6))"
           "(ffi/copy! (ffi/view overlap 1 5) overlap 5)");
  check_printed(rt, "(str (ffi/read-bytes overlap 6))", "#u8(1 1 2 3 4 5)");
  eval(rt, "(ffi/copy! overlap (ffi/view overlap 1 5) 5)");
  check_printed(rt, "(str (ffi/read-bytes overlap 6))", "#u8(1 2 3 4 5 5)");
  eval(rt, "(def detached (ffi/read-bytes overlap 6)) (ffi/write-bytes! overlap #u8(9))");
  check_int(rt, "(bytevector-u8-ref detached 0)", 1);
  check_error(rt, "(ffi/copy! overlap (ffi/view overlap 0 2) 3)", "bounds");
  check_error(rt, "(ffi/copy! (ffi/view overlap 0 2) overlap 3)", "bounds");
  eval(rt, "(ffi/free! overlap)");
  check_error(rt, "(ffi/copy! overlap overlap 0)", "released");

  /* Layouts copy their specifications and outlive redefinition. */
  eval(rt, "(def spec (list 'array 'double 2))"
           "(def fields (list (list 'tag 'u8) (list 'values spec)))"
           "(def Layout (ffi/define-struct! 'Layout fields))"
           "(set-car! (cdr spec) 'u8) (set-car! (car fields) 'changed)");
  check_int(rt, "(ffi/sizeof Layout)", 24);
  check_int(rt, "(ffi/alignof Layout)", 8);
  check_int(rt, "(ffi/offset-of Layout '(values 1))", 16);
  eval(rt,
       "(def saved-layout Layout)"
       "(def Container (ffi/define-struct! 'Container (list (list 'items (list 'array Layout 2)))))"
       "(set! Layout (ffi/define-struct! 'Layout '((tag u8))))"
       "(def aggregate (ffi/alloc Container 1))"
       "(ffi/field-set! aggregate Container '(items 1 values 1) 3.5)");
  check_int(rt, "(ffi/sizeof Container)", 48);
  check_true(rt, "(= (ffi/field-ref aggregate Container '(items 1 values 1)) 3.5)");
  eval(rt, "(def aggregate-view (ffi/field-view aggregate Container '(items 1)))");
  check_true(rt, "(= (ffi/field-ref aggregate-view saved-layout '(values 1)) 3.5)");
  eval(rt, "(def Union (ffi/define-union! 'Union '((tag u8) (value double))))");
  check_int(rt, "(ffi/sizeof Union)", 8);
  check_int(rt, "(ffi/offset-of Union '(value))", 0);
  eval(rt, "(ffi/free! aggregate)");
  check_error(rt, "(ffi/field-ref aggregate-view saved-layout '(tag))", "released");
  check_error(rt, "(ffi/define-struct! 'Duplicate '((a u8) (a double)))", "duplicate");
  check_error(rt, "(ffi/offset-of Container '(items 2))", "bounds");
  check_int(rt, "(ffi/allocation-count)", 0);
  ot_destroy(rt);
}

/* The fixture as a path relative to the working directory. The aggregate
 * checks prefix it with "./" to open the same library under another name. */
static void relative_fixture(char* buffer, size_t size) {
  char directory[1024];
  CHECK(getcwd(directory, sizeof(directory)));
  size_t length = strlen(directory);
  if (strncmp(FIXTURE, directory, length) || FIXTURE[length] != '/')
    test_fail(__FILE__, __LINE__, "%s is not under %s", FIXTURE, directory);
  CHECK((size_t)snprintf(buffer, size, ".%s", &FIXTURE[length]) < size);
}

TEST(aggregate_calls_and_signature_cache) {
  ot_state* rt = core_runtime();
  ot_gc_stress(rt, true);
  char fixture[1024], source[2048];
  relative_fixture(fixture, sizeof(fixture));
  snprintf(source, sizeof(source), "(def aggregate-fixture \"%s\")", fixture);
  eval(rt, source);
  eval_file(rt, "c/tests/unit/data/aggregate.ot");
  size_t prepared = ot_get_stats(rt).signatures;
  eval(rt, "(def SamePoint (ffi/define-struct! 'OtherName '((a double) (b double))))"
           "(def same-translate (ffi/bind aggregate-lib \"translate\" (list SamePoint 'double "
           "'double) SamePoint #t))");
  CHECK(ot_get_stats(rt).signatures == prepared);
  /* Only prepare these signatures: the fixture has a different C type. */
  eval(rt, "(def DifferentPoint (ffi/define-struct! 'Point '((a u64) (b double))))"
           "(def unused-binding (ffi/bind aggregate-lib \"translate\" (list DifferentPoint 'double "
           "'double) DifferentPoint #t))");
  CHECK(ot_get_stats(rt).signatures == prepared + 1);
  eval(rt, "(dotimes (i 40)"
           " (let ((layout (ffi/define-struct! 'Temporary (list (list 'data (list 'array 'u8 (+ i "
           "1)))))))"
           "   (ffi/bind aggregate-lib \"translate\" (list layout) layout #t)))");
  eval(rt, "(+ 20 22)");
  CHECK(ot_get_stats(rt).signatures <= prepared + 6);
  ot_destroy(rt);
}

TEST(common_ffi_script) {
  ot_state* rt = core_runtime();
  const char* arguments[] = {FIXTURE, OTHER};
  CHECK(ot_set_arguments(rt, "tests/ffi/common.ot", 2, arguments));
  ot_gc_stress(rt, true);
  eval_file(rt, "tests/ffi/support.ot");
  eval_file(rt, "tests/ffi/common.ot");
  check_int(rt, "(ffi/allocation-count)", 0);
  ot_destroy(rt);
}

TEST(callbacks_and_callback_lifetime) {
  ot_state* rt = core_runtime();
  char directory[] = "/tmp/otium-callback-test-XXXXXX";
  CHECK(mkdtemp(directory));
  const char* arguments[] = {FIXTURE, OTHER, directory};
  CHECK(ot_set_arguments(rt, "tests/ffi/callbacks.ot", 3, arguments));
  CHECK(ot_add_load_path(rt, directory));
  ot_gc_stress(rt, true);
  eval_file(rt, "tests/ffi/support.ot");
  eval_file(rt, "tests/ffi/callbacks.ot");
  eval(rt, "(def callback-extra (ffi/open-library '(\"" EXTRA "\")))");
  eval_file(rt, "c/tests/unit/data/callback-lifetime.ot");

  /* Freed callbacks release their records. */
  size_t callback_records = ot_get_stats(rt).callback_records;
  for (size_t i = 0; i < 100; i++) {
    eval(rt, "(def temporary-callback (ffi/callback '(int) 'int (fn (x) (+ x 1))))"
             "(ffi/free-callback! temporary-callback) (set! temporary-callback nil)");
    CHECK(ot_get_stats(rt).callback_records <= callback_records + 2);
  }
  eval(rt, "(+ 20 22)");
  CHECK(ot_get_stats(rt).callback_records <= callback_records);
  CHECK(ot_get_stats(rt).callbacks == 1);
  char path[1024];
  snprintf(path, sizeof(path), "%s/ffi-callback-handler.ot", directory);
  CHECK(!unlink(path));
  CHECK(!rmdir(directory));
  ot_destroy(rt);
}

TEST(ownership_scripts) {
  ot_state* rt = core_runtime();
  ot_gc_stress(rt, true);
  eval_file(rt, "tests/ffi/ownership.ot");
  eval_file(rt, "c/tests/unit/data/ownership.ot");
  ot_destroy(rt);
}

TEST(foreign_error_fields) {
  ot_state* rt = core_runtime();
  ot_gc_stress(rt, true);
  eval_file(rt, "c/tests/unit/data/foreign-errors.ot");
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(library_loading_and_binding),
    TEST_ENTRY(integer_types_round_trip),
    TEST_ENTRY(scalar_text_and_pointer_conversions),
    TEST_ENTRY(foreign_memory_and_collection),
    TEST_ENTRY(text_survives_collection_in_small_heap),
    TEST_ENTRY(allocation_reports_heap_exhaustion),
    TEST_ENTRY(bulk_memory_and_layouts),
    TEST_ENTRY(aggregate_calls_and_signature_cache),
    TEST_ENTRY(common_ffi_script),
    TEST_ENTRY(callbacks_and_callback_lifetime),
    TEST_ENTRY(ownership_scripts),
    TEST_ENTRY(foreign_error_fields),
};

TEST_SUITE(foreign, tests);
