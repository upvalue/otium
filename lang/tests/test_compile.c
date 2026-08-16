#define OT_HEAP_INTERNALS
#include "ctest.h"
#include "../src/compile.h"
#include "../src/eval.h"
#include "../src/heap.h"
#include "../src/printer.h"
#include "../src/reader.h"
#include "../src/state.h"
#include "../src/vm.h"
#include <string.h>

static State* compiler_state(u32 maxDepth) {
  StateConfig config = state_config_default();
  config.heapBytes = 512 * 1024;
  config.stackSlots = 16384;
  config.maxDepth = 1000;
  State* state = state_create(&config);
  state->cfg.maxDepth = maxDepth;
  return state;
}

static void repr_value(State* state, Value value, Buf* out) {
  OT_SCOPE(state);
  Ref rooted = ot_push(state);
  ot_set_return(state, rooted, value);
  ot_repr(state, rooted, out);
}

static Value run_compiled(State* state, const char* source) {
  Reader reader;
  reader_init(&reader, state, source, (u32)strlen(source), "<compiler-test>");
  OT_SCOPE(state);
  Ref form = ot_push(state);
  Ref code = ot_push(state);
  Ref result = ot_push(state);
  OT_TRY(reader_next_ref(&reader, form));
  OT_TRY(compile_form_ref(state, code, form));
  OT_TRY(ot_execute_code(state, result, code));
  return ot_ret(state, result);
}

TEST(compiler_emits_literals_branches_and_short_circuit_forms) {
  State* state = compiler_state(1000);
  Value result = run_compiled(state, "(if (and #t 1) (or nil 42) 0)");
  CHECK(result.tag == Tag_Int);
  CHECK(result.i == 42);
  state_destroy(state);
}

TEST(compiler_uses_lexical_slots_for_sequential_let_and_set) {
  State* state = compiler_state(1000);
  Value result = run_compiled(state, "(let ((x 1) (y (+ x 2))) (set! x 4) (+ x y))");
  CHECK(result.tag == Tag_Int);
  CHECK(result.i == 7);
  state_destroy(state);
}

TEST(compiler_roots_let_bindings_while_compiling_allocating_initializers) {
  State* state = compiler_state(1000);
  Value result = run_compiled(state, "(let ((f (fn () 41))) (+ (f) 1))");
  CHECK(result.tag == Tag_Int);
  CHECK(result.i == 42);
  state_destroy(state);
}

TEST(compiler_hoists_defines_out_of_let_bodies) {
  State* state = compiler_state(1000);
  // Mutually recursive defines under a let, capturing each other and the
  // let bindings (the mperm.scm shape).
  Value result = run_compiled(state, "((lambda (n)"
                                     "   (let ((acc 2))"
                                     "     (define (even? k) (if (= k 0) #t (odd? (- k 1))))"
                                     "     (define (odd? k) (if (= k 0) #f (even? (- k 1))))"
                                     "     (set! acc (+ acc (if (even? n) 40 0)))"
                                     "     acc))"
                                     " 10)");
  CHECK(result.tag == Tag_Int);
  CHECK(result.i == 42);
  state_destroy(state);
}

TEST(compiler_creates_flat_closures_for_captured_locals) {
  State* state = compiler_state(1000);
  Value result = run_compiled(state, "((let ((x 40)) (lambda (y) (+ x y))) 2)");
  CHECK(result.tag == Tag_Int);
  CHECK(result.i == 42);
  state_destroy(state);
}

TEST(compiler_hoists_mutually_recursive_internal_definitions) {
  State* state = compiler_state(8);
  Value result = run_compiled(state, "((lambda (n)"
                                     "   (define (even x) (if (= x 0) #t (odd (- x 1))))"
                                     "   (define (odd x) (if (= x 0) #f (even (- x 1))))"
                                     "   (even n))"
                                     " 10000)");
  CHECK(result.tag == Tag_True);
  state_destroy(state);
}

TEST(compiled_globals_resolve_late_and_retain_live_var_cells) {
  State* state = compiler_state(1000);
  Value result = run_compiled(state, "(begin"
                                     "  (define vm-compiled-global 1)"
                                     "  (define (get-vm-compiled-global) vm-compiled-global)"
                                     "  (set! vm-compiled-global 2)"
                                     "  (get-vm-compiled-global))");
  CHECK(result.tag == Tag_Int);
  CHECK(result.i == 2);
  state_destroy(state);
}

TEST(compiler_lowers_while_loops_without_recursive_c_evaluation) {
  State* state = compiler_state(1000);
  Value result = run_compiled(state, "(let ((i 0)) (while (< i 5) (set! i (+ i 1))) i)");
  CHECK(result.tag == Tag_Int);
  CHECK(result.i == 5);
  state_destroy(state);
}

TEST(compiler_lowers_cond_clauses_including_their_truthy_test_value) {
  State* state = compiler_state(1000);
  Value result =
      run_compiled(state, "(list (cond ((= 1 2) 0) ((= 2 2) 42) (else 9)) (cond (#f 1) (7)))");
  CHECK(result.tag == Tag_Pair);
  if (result.tag != Tag_Pair) {
    state_destroy(state);
    return;
  }
  CHECK(as_pair(result)->car.tag == Tag_Int);
  CHECK(as_pair(result)->car.i == 42);
  Value second = as_pair(as_pair(result)->cdr)->car;
  CHECK(second.tag == Tag_Int);
  CHECK(second.i == 7);
  state_destroy(state);
}

TEST(compiler_roots_cond_clauses_while_compiling_allocating_tests) {
  State* state = compiler_state(1000);
  Value result = run_compiled(state, "(cond (((fn () #t)) 42) (else 0))");
  CHECK(result.tag == Tag_Int);
  CHECK(result.i == 42);
  state_destroy(state);
}

TEST(a_bodyless_cond_clause_falls_through_past_a_tail_calling_else) {
  // The bodyless clause's exit jump carries the test value to the end of the
  // cond, so the form falls through even though the else ends in a tail call
  // and emits no return of its own.
  State* state = compiler_state(1000);
  Value result = run_compiled(state, "(do (define (g) 7)"
                                     "    (define (f x) (cond (x) (else (g))))"
                                     "    (list (f 5) (f #f)))");
  CHECK(result.tag == Tag_Pair);
  if (result.tag != Tag_Pair) {
    state_destroy(state);
    return;
  }
  Value taken = as_pair(result)->car;
  Value fallthrough = as_pair(as_pair(result)->cdr)->car;
  CHECK(taken.tag == Tag_Int);
  CHECK(taken.i == 5);
  CHECK(fallthrough.tag == Tag_Int);
  CHECK(fallthrough.i == 7);
  state_destroy(state);
}

TEST(compiler_lowers_quasiquote_and_unquote_splicing) {
  State* state = compiler_state(1000);
  Value result = run_compiled(state, "`(a ,(+ 1 2) ,@(list 4 5) . tail)");
  if (result.tag == Tag_Unwind) {
    Buf condition = {0};
    repr_value(state, state->unwindCondition, &condition);
    fprintf(stderr, "  unwind: %.*s\n", (int)condition.len, condition.data ? condition.data : "");
    buf_deinit(&condition);
  }
  Buf printed = {0};
  repr_value(state, result, &printed);
  CHECK_MEM(printed.data, printed.len, "(a 3 4 5 . tail)");
  buf_deinit(&printed);
  state_destroy(state);
}

TEST(compiled_macros_retain_their_privileged_apply_path) {
  State* state = compiler_state(1000);
  Value result = run_compiled(state, "(begin (defmacro vm-id (x) x) vm-id)");
  CHECK(result.tag == Tag_Macro);
  if (result.tag == Tag_Macro) {
    OT_SCOPE(state);
    Ref macro = ref_push(state, result);
    u32 base = state->stack.len;
    state_push(state, int_v(5));
    Value expanded = apply(state, ref_get(state, macro), base, 1);
    CHECK(expanded.tag == Tag_Int);
    CHECK(expanded.i == 5);
  }
  state_destroy(state);
}

TEST(compiled_function_definitions_retain_names_and_docstrings) {
  State* state = compiler_state(1000);
  Value result = run_compiled(
      state, "(begin (define (vm-documented x) \"compiler docs\" x) (describe 'vm-documented))");
  CHECK(result.tag == Tag_String);
  if (result.tag == Tag_String) {
    StringData* string = as_string(result);
    CHECK_MEM(string_data_bytes(string), string->len, "user/vm-documented: compiler docs");
  }
  state_destroy(state);
}
