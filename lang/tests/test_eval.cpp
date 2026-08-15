// test_eval.cpp — Vm / namespaces / evaluator / conditions / restarts / params.
#include "doctest.h"
#include "../src/vm.hpp"
#include "../src/eval.hpp"
#include "../src/ns.hpp"
#include "../src/reader.hpp"

using namespace ot;

// Minimal arithmetic natives so tests don't depend on the builtins agent.
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

TEST_CASE("million-iteration mutual tail recursion under small depth cap") {
  Vm* vm = mkvm(200);
  Value r = run(*vm, "(define (ev n) (if (= n 0) #t (od (- n 1))))"
                     "(define (od n) (if (= n 0) #f (ev (- n 1))))"
                     "(ev 1000000)");
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
