// test_eval.c — State / namespaces / evaluator / conditions / restarts / params.
#include "ctest.h"
#include "../src/state.h"
#include "../src/eval.h"
#include "../src/ns.h"
#include "../src/reader.h"
#include "../src/builtins.h"
#include <string.h>

// Minimal arithmetic natives keep evaluator tests independent of builtin
// registration.
static Value t_add(State* vm, u32 base, u32 argc) {
  i64 s = 0;
  for (u32 i = 0; i < argc; i++) s += vm->stack.data[base + i].i;
  return int_v(s);
}
static Value t_sub(State* vm, u32 base, u32 argc) {
  if (argc == 1) return int_v(-vm->stack.data[base].i);
  i64 s = vm->stack.data[base].i;
  for (u32 i = 1; i < argc; i++) s -= vm->stack.data[base + i].i;
  return int_v(s);
}
static Value t_trip(State* vm, u32 base, u32 argc) {  // request interruption
  (void)base;
  (void)argc;
  vm->interruptFlag = true;
  return nil_v();
}
static Value t_numeq(State* vm, u32 base, u32 argc) {
  for (u32 i = 1; i < argc; i++)
    if (vm->stack.data[base].i != vm->stack.data[base + i].i) return bool_v(false);
  return bool_v(true);
}

static u32 native_init_calls = 0;
static u32 native_init_ns = 0;

static Value t_native_answer(State* vm, u32 base, u32 argc) {
  (void)vm;
  (void)base;
  (void)argc;
  return int_v(40);
}

static void t_native_init(State* vm) {
  native_init_calls++;
  native_init_ns = vm->currentNs;
  def_native(vm, "native-answer", t_native_answer);
  // An extension may enter a sub-namespace while installing more bindings.
  // The require harness must still load sugar in the module and restore its caller.
  ns_switch(vm, intern_id(&vm->intern, "test.native.sub", 15));
}

typedef struct TestLoader {  // zero-init
  u32 calls;
  bool sugar;
} TestLoader;

static bool t_load(void* ud, const char* nsName, Buf* out) {
  TestLoader* loader = (TestLoader*)ud;
  loader->calls++;
  if (loader->sugar && strcmp(nsName, "test.native") == 0) {
    buf_append_cstr(out, "(define sugar (+ (native-answer) 2))");
    return true;
  }
  if (strcmp(nsName, "test.source") == 0) {
    buf_append_cstr(out, "(define source-value 9)");
    return true;
  }
  return false;
}

static State* mkvm(u32 maxDepth) {
  StateConfig cfg = state_config_default();
  cfg.heapBytes = 4u << 20;
  cfg.stackSlots = 8192;
  cfg.maxDepth = maxDepth;
  State* vm = state_create(&cfg);
  u32 saved = vm->currentNs;
  vm->currentNs = vm->syms.otiumCore_;
  ns_define(vm, intern_id(&vm->intern, "+", 1), make_native(vm, "+", t_add), false, nil_v());
  ns_define(vm, intern_id(&vm->intern, "-", 1), make_native(vm, "-", t_sub), false, nil_v());
  ns_define(vm, intern_id(&vm->intern, "=", 1), make_native(vm, "=", t_numeq), false, nil_v());
  ns_define(vm, intern_id(&vm->intern, "trip!", 5), make_native(vm, "trip!", t_trip), false,
            nil_v());
  vm->currentNs = saved;
  // `user` was created before these defs; run tests from a fresh namespace
  // created now, whose auto-refer snapshot includes them.
  ns_switch(vm, intern_id(&vm->intern, "test.main", 9));
  return vm;
}

static Value run(State* vm, const char* src) {
  Reader rd;
  reader_init(&rd, vm, src, (u32)strlen(src), "test");
  Value last = nil_v();
  for (;;) {
    Value f = reader_next(&rd);
    if (f.tag == Tag_Unwind) return f;
    if (reader_at_eof(&rd)) break;
    last = eval_form(vm, f);
    if (last.tag == Tag_Unwind) return last;
  }
  return last;
}

static bool is_int(Value v, i64 n) { return v.tag == Tag_Int && v.i == n; }

static bool condition_message_is(State* vm, const char* expected) {
  Value message = table_get(vm, vm->unwindCondition, keyword_v(vm->syms.kwMessage));
  if (message.tag != Tag_String) return false;
  StringData* s = as_string(message);
  return s->len == (u32)strlen(expected) &&
         memcmp(string_data_bytes(s), expected, s->len) == 0;
}

TEST(native_validators_report_consistent_arity_and_type_errors) {
  State* vm = mkvm(2000);

  Value r = run(vm, "(string-length)");
  CHECK(r.tag == Tag_Unwind);
  CHECK(condition_message_is(vm, "string-length: wrong number of arguments (0)"));
  state_cancel_unwind(vm);

  r = run(vm, "(car)");
  CHECK(r.tag == Tag_Unwind);
  CHECK(condition_message_is(vm, "car: wrong number of arguments (0)"));
  state_cancel_unwind(vm);

  r = run(vm, "(newline 1)");
  CHECK(r.tag == Tag_Unwind);
  CHECK(condition_message_is(vm, "newline: wrong number of arguments (1)"));
  state_cancel_unwind(vm);

  r = run(vm, "(string-length 1)");
  CHECK(r.tag == Tag_Unwind);
  CHECK(condition_message_is(vm, "string-length: expected string"));
  state_cancel_unwind(vm);

  r = run(vm, "(car 1)");
  CHECK(r.tag == Tag_Unwind);
  CHECK(condition_message_is(vm, "car: expected pair"));
  state_cancel_unwind(vm);

  state_destroy(vm);
}

TEST(eval_source_shares_eof_unwind_and_consumed_prefix_semantics) {
  State* vm = mkvm(2000);
  const char* complete = "(define answer 40) (+ answer 2)";
  CHECK(is_int(eval_source(vm, complete, (u32)strlen(complete), "test"), 42));

  const char* partial = "(define hits 0) (set! hits (+ hits 1)) (+ 1";
  EvalSourceState state = {0};
  EvalSourcePolicy policy = {0};
  policy.state = &state;
  Value result = eval_source_policy(vm, partial, (u32)strlen(partial), "test", &policy);
  CHECK(result.tag == Tag_Unwind);
  CHECK(state.readError);
  CHECK(state.incomplete);
  CHECK(state.consumed > 0);
  CHECK(state.consumed < (u32)strlen(partial));
  state_cancel_unwind(vm);
  CHECK(is_int(run(vm, "hits"), 1));
  state_destroy(vm);
}

TEST(closures_capture_and_let_is_sequential) {
  State* vm = mkvm(2000);
  Value r = run(vm, "(define (mk n) (lambda (x) (+ x n)))"
                    "(define f (mk 10))"
                    "(f 5)");
  CHECK(is_int(r, 15));
  r = run(vm, "(let ((a 1) (b (+ a 1))) (+ a b))");
  CHECK(is_int(r, 3));
  r = run(vm, "(let ((x 1)) (set! x 41) (+ x 1))");
  CHECK(is_int(r, 42));
  state_destroy(vm);
}

TEST(namespace_switching_and_qualified_refs) {
  State* vm = mkvm(2000);
  Value r = run(vm, "(ns t.a) (define x 41) (define- hidden 9)"
                    "(ns t.b) (define x 1)"
                    "(+ t.a/x x)");
  CHECK(is_int(r, 42));
  // alias via require :as
  r = run(vm, "(require (t.a :as ta)) ta/x");
  CHECK(is_int(r, 41));
  // private var cross-namespace is an error
  r = run(vm, "t.a/hidden");
  CHECK(r.tag == Tag_Unwind);
  CHECK(vm->unwindKind == UnwindKind_Condition);
  vm->unwindKind = UnwindKind_None;
  state_destroy(vm);
}

TEST(require_initializes_native_modules_before_optional_source_sugar) {
  native_init_calls = 0;
  native_init_ns = 0;
  State* vm = mkvm(2000);
  u32 callerNs = vm->currentNs;
  u32 targetNs = intern_id(&vm->intern, "test.native", 11);
  register_native_module(vm, "test.native", t_native_init);
  TestLoader loader = {0};
  loader.sugar = true;
  vm->loadFn = t_load;
  vm->loadUd = &loader;

  Value r = run(vm, "(require 'test.native) test.native/sugar");
  CHECK(is_int(r, 42));
  CHECK(native_init_calls == 1);
  CHECK(native_init_ns == targetNs);
  CHECK(vm->currentNs == callerNs);
  CHECK(loader.calls == 1);

  r = run(vm, "(require 'test.native) (test.native/native-answer)");
  CHECK(is_int(r, 40));
  CHECK(native_init_calls == 1);
  CHECK(loader.calls == 1);

  // A registered module needs no companion source file.
  register_native_module(vm, "test.bare", t_native_init);
  r = run(vm, "(require 'test.bare) (test.bare/native-answer)");
  CHECK(is_int(r, 40));
  CHECK(native_init_calls == 2);
  CHECK(vm->currentNs == callerNs);

  // Unregistered modules retain the original source-only and missing paths.
  r = run(vm, "(require 'test.source) test.source/source-value");
  CHECK(is_int(r, 9));
  r = run(vm, "(require 'test.missing)");
  CHECK(is_unwind(r));
  CHECK(condition_message_is(vm, "namespace not found on load path: test.missing"));
  state_cancel_unwind(vm);
  state_destroy(vm);
}

TEST(in_ns_is_a_special_form_with_consistent_name_handling) {
  State* vm = mkvm(2000);
  Value r = run(vm, "(ns t.symbol) (define marker 1)"
                    "(ns t.keyword) (define marker 2)"
                    "(ns t.string) (define marker 3)"
                    "(in-ns 't.symbol) marker");
  CHECK(is_int(r, 1));
  r = run(vm, "(in-ns :t.keyword) marker");
  CHECK(is_int(r, 2));
  r = run(vm, "(in-ns \"t.string\") marker");
  CHECK(is_int(r, 3));

  // Like other special forms, in-ns has no independent higher-order value.
  r = run(vm, "in-ns");
  CHECK(r.tag == Tag_Unwind);
  vm->unwindKind = UnwindKind_None;
  state_destroy(vm);
}

TEST(three_million_iteration_mutual_tail_recursion_under_small_depth_cap) {
  State* vm = mkvm(200);
#ifdef OT_GC_STRESS
  const char* iterations = "1000";
#else
  const char* iterations = "3000000";
#endif
  char source[512];
  snprintf(source, sizeof source,
           "(define (ev n) (if (= n 0) #t (od (- n 1))))"
           "(define (od n) (if (= n 0) #f (ev (- n 1))))"
           "(ev %s)",
           iterations);
  Value r = run(vm, source);
  CHECK(r.tag == Tag_True);
  // non-tail recursion past the cap is a catchable error, not a crash
  r = run(vm, "(define (boom n) (+ 1 (boom (- n 1))))"
              "(try (boom 100000) (catch ((lambda (c) #t) c) :caught))");
  CHECK(r.tag == Tag_Keyword);
  state_destroy(vm);
}

TEST(handler_declines_outer_handler_handles) {
  State* vm = mkvm(2000);
  Value r = run(vm, "(define hits 0)"
                    "(handler-bind (((lambda (c) #t) (lambda (c) (set! hits (+ hits 2)))))"
                    "  (handler-bind (((lambda (c) #t) (lambda (c) (set! hits (+ hits 1)))))"
                    "    (signal 'boom)))"
                    "hits");
  CHECK(is_int(r, 3));  // inner ran first (declined), then outer
  state_destroy(vm);
}

TEST(handler_invokes_a_restart_below_it_computation_resumes) {
  State* vm = mkvm(2000);
  Value r = run(vm, "(define (compute)"
                    "  (restart-case (begin (error \"boom\") 99)"
                    "    (use-value \"substitute a value\" (v) (+ v 1))))"
                    "(handler-bind (((lambda (c) #t)"
                    "                (lambda (c) (invoke-restart 'use-value 41))))"
                    "  (compute))");
  CHECK(is_int(r, 42));
  state_destroy(vm);
}

TEST(unwind_protect_cleanups_run_under_restart_transfer) {
  State* vm = mkvm(2000);
  Value r = run(vm, "(define cleaned 0)"
                    "(handler-bind (((lambda (c) #t)"
                    "                (lambda (c) (invoke-restart 'give 5))))"
                    "  (restart-case"
                    "      (unwind-protect (error \"x\") (set! cleaned 1))"
                    "    (give (v) (+ v cleaned))))");
  CHECK(is_int(r, 6));  // cleanup ran (cleaned=1) before the clause body
  state_destroy(vm);
}

TEST(try_catches_unwinding_conditions_but_never_quit) {
  State* vm = mkvm(2000);
  Value r = run(vm, "(try (error \"b\") (catch ((lambda (c) #t) c) :caught))");
  CHECK(r.tag == Tag_Keyword);

  // quit passes through try; unwind-protect cleanups still run
  Value r2 = run(vm, "(define observed 0)");
  CHECK(!is_unwind(r2));
  r = run(vm, "(try (unwind-protect (begin (trip!) 1 2) (set! observed 1))"
              "  (catch ((lambda (c) #t) c) :caught))");
  CHECK(r.tag == Tag_Unwind);
  CHECK(vm->unwindKind == UnwindKind_Quit);
  vm->unwindKind = UnwindKind_None;
  Value obs = run(vm, "observed");
  CHECK(is_int(obs, 1));
  state_destroy(vm);
}

TEST(dynamic_params_defaults_with_params_removal_on_unwind) {
  State* vm = mkvm(2000);
  Value r = run(vm, "(defparam *w* 80)"
                    "(define (width) (*w*))"
                    "(+ (width) (with-params ((*w* 20)) (width)))");
  CHECK(is_int(r, 100));
  // binding removed on raise path
  r = run(vm, "(try (with-params ((*w* 5)) (error \"x\")) (catch ((lambda (c) #t) c) (width)))");
  CHECK(is_int(r, 80));
  // defparam inside a body is an error
  r = run(vm, "(let ((q 1)) (defparam *bad* 0))");
  CHECK(r.tag == Tag_Unwind);
  vm->unwindKind = UnwindKind_None;
  state_destroy(vm);
}

TEST(condition_types_and_macros) {
  State* vm = mkvm(2000);
  Value r = run(vm, "(define-condition 'file-error 'error)"
                    "(condition-of-type? {:type 'file-error} 'error)");
  CHECK(r.tag == Tag_True);
  r = run(vm, "(defmacro twice (x) `(+ ,x ,x))"
              "(twice 21)");
  CHECK(is_int(r, 42));
  state_destroy(vm);
}
