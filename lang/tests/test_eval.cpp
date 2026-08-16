// test_eval.cpp — Vm / namespaces / evaluator / conditions / restarts / params.
#include "doctest.h"
#include "../src/vm.hpp"
#include "../src/eval.hpp"
#include "../src/ns.hpp"
#include "../src/reader.hpp"
#include "../src/builtins.hpp"
#include <string>

using namespace ot;

// Minimal arithmetic natives keep evaluator tests independent of builtin
// registration.
static Value t_add(Vm& vm, u32 base, u32 argc) {
  i64 s = 0;
  for (u32 i = 0; i < argc; i++) s += vm.stack[base + i].i;
  return int_v(s);
}
static Value t_sub(Vm& vm, u32 base, u32 argc) {
  if (argc == 1) return int_v(-vm.stack[base].i);
  i64 s = vm.stack[base].i;
  for (u32 i = 1; i < argc; i++) s -= vm.stack[base + i].i;
  return int_v(s);
}
static Value t_trip(Vm& vm, u32 base, u32 argc) {  // request interruption
  (void)base;
  (void)argc;
  vm.interruptFlag = true;
  return nil_v();
}
static Value t_numeq(Vm& vm, u32 base, u32 argc) {
  for (u32 i = 1; i < argc; i++)
    if (vm.stack[base].i != vm.stack[base + i].i) return bool_v(false);
  return bool_v(true);
}

static u32 native_init_calls = 0;
static u32 native_init_ns = 0;

static Value t_native_answer(Vm&, u32, u32) { return int_v(40); }

static void t_native_init(Vm& vm) {
  native_init_calls++;
  native_init_ns = vm.currentNs;
  def_native(vm, "native-answer", t_native_answer);
  // An extension may enter a sub-namespace while installing more bindings.
  // The require harness must still load sugar in the module and restore its caller.
  ns_switch(vm, vm.intern.intern("test.native.sub", 15));
}

struct TestLoader {
  u32 calls = 0;
  bool sugar = false;
};

static bool t_load(void* ud, const char* nsName, Buf* out) {
  TestLoader& loader = *(TestLoader*)ud;
  loader.calls++;
  if (loader.sugar && strcmp(nsName, "test.native") == 0) {
    out->appendCstr("(define sugar (+ (native-answer) 2))");
    return true;
  }
  if (strcmp(nsName, "test.source") == 0) {
    out->appendCstr("(define source-value 9)");
    return true;
  }
  return false;
}

static Vm* mkvm(u32 maxDepth = 2000) {
  VmConfig cfg{4u << 20, 8192, maxDepth};
  Vm* vm = Vm::create(cfg);
  u32 saved = vm->currentNs;
  vm->currentNs = vm->syms.otiumCore_;
  ns_define(*vm, vm->intern.intern("+", 1), make_native(*vm, "+", t_add), false, nil_v());
  ns_define(*vm, vm->intern.intern("-", 1), make_native(*vm, "-", t_sub), false, nil_v());
  ns_define(*vm, vm->intern.intern("=", 1), make_native(*vm, "=", t_numeq), false, nil_v());
  ns_define(*vm, vm->intern.intern("trip!", 5), make_native(*vm, "trip!", t_trip), false, nil_v());
  vm->currentNs = saved;
  // `user` was created before these defs; run tests from a fresh namespace
  // created now, whose auto-refer snapshot includes them.
  ns_switch(*vm, vm->intern.intern("test.main", 9));
  return vm;
}

static Value run(Vm& vm, const char* src) {
  Reader rd(vm, src, (u32)strlen(src), "test");
  Value last = nil_v();
  for (;;) {
    Value f = rd.next();
    if (f.tag == Tag::Unwind) return f;
    if (rd.atEof()) break;
    last = eval_form(vm, f);
    if (last.tag == Tag::Unwind) return last;
  }
  return last;
}

static bool is_int(Value v, i64 n) { return v.tag == Tag::Int && v.i == n; }

static std::string condition_message(Vm& vm) {
  Value message = table_get(vm, vm.unwindCondition, keyword_v(vm.syms.kwMessage));
  if (message.tag != Tag::String) return {};
  StringData* s = as_string(message);
  return std::string(string_bytes(s), s->len);
}

TEST_CASE("native validators report consistent arity and type errors") {
  Vm* vm = mkvm();

  Value r = run(*vm, "(string-length)");
  CHECK(r.tag == Tag::Unwind);
  CHECK(condition_message(*vm) == "string-length: wrong number of arguments (0)");
  vm_cancel_unwind(*vm);

  r = run(*vm, "(car)");
  CHECK(r.tag == Tag::Unwind);
  CHECK(condition_message(*vm) == "car: wrong number of arguments (0)");
  vm_cancel_unwind(*vm);

  r = run(*vm, "(newline 1)");
  CHECK(r.tag == Tag::Unwind);
  CHECK(condition_message(*vm) == "newline: wrong number of arguments (1)");
  vm_cancel_unwind(*vm);

  r = run(*vm, "(string-length 1)");
  CHECK(r.tag == Tag::Unwind);
  CHECK(condition_message(*vm) == "string-length: expected string");
  vm_cancel_unwind(*vm);

  r = run(*vm, "(car 1)");
  CHECK(r.tag == Tag::Unwind);
  CHECK(condition_message(*vm) == "car: expected pair");
  vm_cancel_unwind(*vm);

  vm->destroy();
}

TEST_CASE("eval_source shares EOF, unwind, and consumed-prefix semantics") {
  Vm* vm = mkvm();
  const char* complete = "(define answer 40) (+ answer 2)";
  CHECK(is_int(eval_source(*vm, complete, (u32)strlen(complete), "test"), 42));

  const char* partial = "(define hits 0) (set! hits (+ hits 1)) (+ 1";
  EvalSourceState state;
  EvalSourcePolicy policy;
  policy.state = &state;
  Value result = eval_source(*vm, partial, (u32)strlen(partial), "test", policy);
  CHECK(result.tag == Tag::Unwind);
  CHECK(state.readError);
  CHECK(state.incomplete);
  CHECK(state.consumed > 0);
  CHECK(state.consumed < (u32)strlen(partial));
  vm_cancel_unwind(*vm);
  CHECK(is_int(run(*vm, "hits"), 1));
  vm->destroy();
}

TEST_CASE("closures capture and let is sequential") {
  Vm* vm = mkvm();
  Value r = run(*vm, "(define (mk n) (lambda (x) (+ x n)))"
                     "(define f (mk 10))"
                     "(f 5)");
  CHECK(is_int(r, 15));
  r = run(*vm, "(let ((a 1) (b (+ a 1))) (+ a b))");
  CHECK(is_int(r, 3));
  r = run(*vm, "(let ((x 1)) (set! x 41) (+ x 1))");
  CHECK(is_int(r, 42));
  vm->destroy();
}

TEST_CASE("namespace switching and qualified refs") {
  Vm* vm = mkvm();
  Value r = run(*vm, "(ns t.a) (define x 41) (define- hidden 9)"
                     "(ns t.b) (define x 1)"
                     "(+ t.a/x x)");
  CHECK(is_int(r, 42));
  // alias via require :as
  r = run(*vm, "(require (t.a :as ta)) ta/x");
  CHECK(is_int(r, 41));
  // private var cross-namespace is an error
  r = run(*vm, "t.a/hidden");
  CHECK(r.tag == Tag::Unwind);
  CHECK(vm->unwindKind == UnwindKind::Condition);
  vm->unwindKind = UnwindKind::None;
  vm->destroy();
}

TEST_CASE("require initializes native modules before optional source sugar") {
  native_init_calls = 0;
  native_init_ns = 0;
  Vm* vm = mkvm();
  u32 callerNs = vm->currentNs;
  u32 targetNs = vm->intern.intern("test.native", 11);
  register_native_module(*vm, "test.native", t_native_init);
  TestLoader loader;
  loader.sugar = true;
  vm->loadFn = t_load;
  vm->loadUd = &loader;

  Value r = run(*vm, "(require 'test.native) test.native/sugar");
  CHECK(is_int(r, 42));
  CHECK(native_init_calls == 1);
  CHECK(native_init_ns == targetNs);
  CHECK(vm->currentNs == callerNs);
  CHECK(loader.calls == 1);

  r = run(*vm, "(require 'test.native) (test.native/native-answer)");
  CHECK(is_int(r, 40));
  CHECK(native_init_calls == 1);
  CHECK(loader.calls == 1);

  // A registered module needs no companion source file.
  register_native_module(*vm, "test.bare", t_native_init);
  r = run(*vm, "(require 'test.bare) (test.bare/native-answer)");
  CHECK(is_int(r, 40));
  CHECK(native_init_calls == 2);
  CHECK(vm->currentNs == callerNs);

  // Unregistered modules retain the original source-only and missing paths.
  r = run(*vm, "(require 'test.source) test.source/source-value");
  CHECK(is_int(r, 9));
  r = run(*vm, "(require 'test.missing)");
  CHECK(is_unwind(r));
  CHECK(condition_message(*vm) == "namespace not found on load path: test.missing");
  vm_cancel_unwind(*vm);
  vm->destroy();
}

TEST_CASE("in-ns is a special form with consistent name handling") {
  Vm* vm = mkvm();
  Value r = run(*vm, "(ns t.symbol) (define marker 1)"
                     "(ns t.keyword) (define marker 2)"
                     "(ns t.string) (define marker 3)"
                     "(in-ns 't.symbol) marker");
  CHECK(is_int(r, 1));
  r = run(*vm, "(in-ns :t.keyword) marker");
  CHECK(is_int(r, 2));
  r = run(*vm, "(in-ns \"t.string\") marker");
  CHECK(is_int(r, 3));

  // Like other special forms, in-ns has no independent higher-order value.
  r = run(*vm, "in-ns");
  CHECK(r.tag == Tag::Unwind);
  vm->unwindKind = UnwindKind::None;
  vm->destroy();
}

TEST_CASE("million-iteration mutual tail recursion under small depth cap") {
  Vm* vm = mkvm(200);
#ifdef OT_GC_STRESS
  const char* iterations = "1000";
#else
  const char* iterations = "1000000";
#endif
  std::string source = "(define (ev n) (if (= n 0) #t (od (- n 1))))"
                       "(define (od n) (if (= n 0) #f (ev (- n 1))))"
                       "(ev ";
  source += iterations;
  source += ")";
  Value r = run(*vm, source.c_str());
  CHECK(r.tag == Tag::True);
  // non-tail recursion past the cap is a catchable error, not a crash
  r = run(*vm, "(define (boom n) (+ 1 (boom (- n 1))))"
               "(try (boom 100000) (catch ((lambda (c) #t) c) :caught))");
  CHECK(r.tag == Tag::Keyword);
  vm->destroy();
}

TEST_CASE("handler declines, outer handler handles") {
  Vm* vm = mkvm();
  Value r = run(*vm, "(define hits 0)"
                     "(handler-bind (((lambda (c) #t) (lambda (c) (set! hits (+ hits 2)))))"
                     "  (handler-bind (((lambda (c) #t) (lambda (c) (set! hits (+ hits 1)))))"
                     "    (signal 'boom)))"
                     "hits");
  CHECK(is_int(r, 3));  // inner ran first (declined), then outer
  vm->destroy();
}

TEST_CASE("handler invokes a restart below it; computation resumes") {
  Vm* vm = mkvm();
  Value r = run(*vm, "(define (compute)"
                     "  (restart-case (begin (error \"boom\") 99)"
                     "    (use-value \"substitute a value\" (v) (+ v 1))))"
                     "(handler-bind (((lambda (c) #t)"
                     "                (lambda (c) (invoke-restart 'use-value 41))))"
                     "  (compute))");
  CHECK(is_int(r, 42));
  vm->destroy();
}

TEST_CASE("unwind-protect cleanups run under restart transfer") {
  Vm* vm = mkvm();
  Value r = run(*vm, "(define cleaned 0)"
                     "(handler-bind (((lambda (c) #t)"
                     "                (lambda (c) (invoke-restart 'give 5))))"
                     "  (restart-case"
                     "      (unwind-protect (error \"x\") (set! cleaned 1))"
                     "    (give (v) (+ v cleaned))))");
  CHECK(is_int(r, 6));  // cleanup ran (cleaned=1) before the clause body
  vm->destroy();
}

TEST_CASE("try catches unwinding conditions but never quit") {
  Vm* vm = mkvm();
  Value r = run(*vm, "(try (error \"b\") (catch ((lambda (c) #t) c) :caught))");
  CHECK(r.tag == Tag::Keyword);

  // quit passes through try; unwind-protect cleanups still run
  Value r2 = run(*vm, "(define observed 0)");
  CHECK(!is_unwind(r2));
  r = run(*vm, "(try (unwind-protect (begin (trip!) 1 2) (set! observed 1))"
               "  (catch ((lambda (c) #t) c) :caught))");
  CHECK(r.tag == Tag::Unwind);
  CHECK(vm->unwindKind == UnwindKind::Quit);
  vm->unwindKind = UnwindKind::None;
  Value obs = run(*vm, "observed");
  CHECK(is_int(obs, 1));
  vm->destroy();
}

TEST_CASE("dynamic params: defaults, with-params, removal on unwind") {
  Vm* vm = mkvm();
  Value r = run(*vm, "(defparam *w* 80)"
                     "(define (width) (*w*))"
                     "(+ (width) (with-params ((*w* 20)) (width)))");
  CHECK(is_int(r, 100));
  // binding removed on raise path
  r = run(*vm, "(try (with-params ((*w* 5)) (error \"x\")) (catch ((lambda (c) #t) c) (width)))");
  CHECK(is_int(r, 80));
  // defparam inside a body is an error
  r = run(*vm, "(let ((q 1)) (defparam *bad* 0))");
  CHECK(r.tag == Tag::Unwind);
  vm->unwindKind = UnwindKind::None;
  vm->destroy();
}

TEST_CASE("condition types and macros") {
  Vm* vm = mkvm();
  Value r = run(*vm, "(define-condition 'file-error 'error)"
                     "(condition-of-type? {:type 'file-error} 'error)");
  CHECK(r.tag == Tag::True);
  r = run(*vm, "(defmacro twice (x) `(+ ,x ,x))"
               "(twice 21)");
  CHECK(is_int(r, 42));
  vm->destroy();
}
