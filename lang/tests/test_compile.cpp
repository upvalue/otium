#include "doctest.h"
#include "../src/compile.hpp"
#include "../src/eval.hpp"
#include "../src/heap.hpp"
#include "../src/printer.hpp"
#include "../src/reader.hpp"
#include "../src/state.hpp"
#include "../src/vm.hpp"
#include <cstring>

using namespace ot;

static State* compiler_state(u32 maxDepth = 1000) {
  StateConfig config{512 * 1024, 16384, 1000};
  State* state = State::create(config);
  state->cfg.maxDepth = maxDepth;
  return state;
}

static Value run_compiled(State& state, const char* source) {
  Reader reader(state, source, (u32)strlen(source), "<compiler-test>");
  Scope roots(state);
  Slot form = roots.push(reader.next());
  if (form.get().tag == Tag::Unwind) return form.get();
  Slot code = roots.push(compile_form(state, form.get()));
  if (code.get().tag == Tag::Unwind) return code.get();
  return vm_execute_code(state, code.get());
}

TEST_CASE("compiler emits literals, branches, and short circuit forms") {
  State* state = compiler_state();
  Value result = run_compiled(*state, "(if (and #t 1) (or nil 42) 0)");
  CHECK(result.tag == Tag::Int);
  CHECK(result.i == 42);
  state->destroy();
}

TEST_CASE("compiler uses lexical slots for sequential let and set") {
  State* state = compiler_state();
  Value result = run_compiled(*state, "(let ((x 1) (y (+ x 2))) (set! x 4) (+ x y))");
  CHECK(result.tag == Tag::Int);
  CHECK(result.i == 7);
  state->destroy();
}

TEST_CASE("compiler creates flat closures for captured locals") {
  State* state = compiler_state();
  Value result = run_compiled(*state, "((let ((x 40)) (lambda (y) (+ x y))) 2)");
  CHECK(result.tag == Tag::Int);
  CHECK(result.i == 42);
  state->destroy();
}

TEST_CASE("compiler hoists mutually recursive internal definitions") {
  State* state = compiler_state(8);
  Value result = run_compiled(
      *state,
      "((lambda (n)"
      "   (define (even x) (if (= x 0) #t (odd (- x 1))))"
      "   (define (odd x) (if (= x 0) #f (even (- x 1))))"
      "   (even n))"
      " 10000)");
  CHECK(result.tag == Tag::True);
  state->destroy();
}

TEST_CASE("compiled globals resolve late and retain live var cells") {
  State* state = compiler_state();
  Value result = run_compiled(*state,
                              "(begin"
                              "  (define vm-compiled-global 1)"
                              "  (define (get-vm-compiled-global) vm-compiled-global)"
                              "  (set! vm-compiled-global 2)"
                              "  (get-vm-compiled-global))");
  CHECK(result.tag == Tag::Int);
  CHECK(result.i == 2);
  state->destroy();
}

TEST_CASE("compiler lowers while loops without recursive C evaluation") {
  State* state = compiler_state();
  Value result =
      run_compiled(*state, "(let ((i 0)) (while (< i 5) (set! i (+ i 1))) i)");
  CHECK(result.tag == Tag::Int);
  CHECK(result.i == 5);
  state->destroy();
}

TEST_CASE("compiler lowers cond clauses including their truthy test value") {
  State* state = compiler_state();
  Value result = run_compiled(
      *state, "(list (cond ((= 1 2) 0) ((= 2 2) 42) (else 9)) (cond (#f 1) (7)))");
  REQUIRE(result.tag == Tag::Pair);
  CHECK(as_pair(result)->car.tag == Tag::Int);
  CHECK(as_pair(result)->car.i == 42);
  Value second = as_pair(as_pair(result)->cdr)->car;
  CHECK(second.tag == Tag::Int);
  CHECK(second.i == 7);
  state->destroy();
}

TEST_CASE("compiler lowers quasiquote and unquote splicing") {
  State* state = compiler_state();
  Value result = run_compiled(*state, "`(a ,(+ 1 2) ,@(list 4 5) . tail)");
  Buf condition;
  if (result.tag == Tag::Unwind) print_repr(*state, state->unwindCondition, condition);
  INFO(std::string(condition.data ? condition.data : "", condition.len));
  Buf printed;
  print_repr(*state, result, printed);
  CHECK(std::string(printed.data, printed.len) == "(a 3 4 5 . tail)");
  state->destroy();
}

TEST_CASE("compiled macros retain their privileged apply path") {
  State* state = compiler_state();
  Value result = run_compiled(*state, "(begin (defmacro vm-id (x) x) vm-id)");
  REQUIRE(result.tag == Tag::Macro);
  {
    Scope roots(*state);
    Slot macro = roots.push(result);
    u32 base = state->stack.len;
    state->push(int_v(5));
    Value expanded = apply(*state, macro.get(), base, 1);
    CHECK(expanded.tag == Tag::Int);
    CHECK(expanded.i == 5);
  }
  state->destroy();
}
