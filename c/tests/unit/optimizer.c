#include "helpers.h"
#include "internal.h"

/* The C interface of the optimizing compiler: global resolution, cell facts,
 * %vm-install with hand-built units, and the evaluation hook. The units and
 * the optimizer that installs them are in data/vm-units.ot. */

static ot_state* units_runtime(bool stress) {
  ot_state* rt = new_core_runtime();
  eval_file(rt, "c/tests/unit/data/vm-units.ot");
  ot_gc_stress(rt, stress);
  return rt;
}

TEST(optimizer_cells) {
  ot_state* rt = new_core_runtime();
  check_true(rt, "(= (%resolve-global 'user 'car) (%resolve-global 'user 'otium.core/car))");
  check_true(rt,
             "(let ((info (%cell-info (%resolve-global 'user 'car)))) "
             "(and (eq? (info :name) 'otium.core/car) (eq? (info :ns) 'otium.core) "
             "(info :defined) (not (info :macro)) (eq? (info :builtin) 'car) (info :inlined)))");
  check_true(rt, "(let ((info (%cell-info (%resolve-global 'user 'length)))) "
                 "(and (eq? (info :builtin) 'length) (not (info :inlined))))");
  check_true(rt, "(let ((info (%cell-info (%resolve-global 'user 'when)))) "
                 "(and (info :macro) (not (info :builtin))))");
  /* An unknown name gets an unbound cell in the namespace, as compiler.c makes. */
  check_true(rt, "(let ((info (%cell-info (%resolve-global 'user 'not-yet-defined)))) "
                 "(and (eq? (info :name) 'user/not-yet-defined) (not (info :defined))))");
  check_true(rt, "(= (%resolve-global 'user 'not-yet-defined) "
                 "(%global-cell 'user 'not-yet-defined))");
  check_true(rt, "(def (later) 1) (= (%global-cell 'user 'later) (%resolve-global 'user 'later))");
  /* A user cell holding a builtin is not the builtin's own cell. */
  check_true(rt, "(def my-car car) (not ((%cell-info (%resolve-global 'user 'my-car)) :builtin))");
  check_error(rt, "(%resolve-global 'no.such.space 'car)", "unknown namespace");
  check_error(rt, "(%cell-info -1)", "global cell id");
  ot_destroy(rt);
}

TEST(optimizer_constants) {
  ot_state* rt = new_core_runtime();
  CHECK(eval(rt, "(let ((ops (%vm-opcodes))) (length ops))") == ot_int(OP_COUNT));
  check_int(rt, "((%vm-opcodes) :move)", OP_MOVE);
  check_int(rt, "((%vm-opcodes) :call-global)", OP_CALL_GLOBAL);
  check_int(rt, "((%vm-opcodes) :br-lti)", OP_BR_LTI);
  check_int(rt, "((%vm-opcodes) :br-eqk)", OP_BR_EQK);
  check_int(rt, "((%vm-opcodes) :return-imm)", OP_RETURN_IMM);
  check_int(rt, "(%immediate-bits 5)", (intptr_t)ot_int(5));
  check_int(rt, "(%immediate-bits -1)", (intptr_t)(int32_t)ot_int(-1));
  check_int(rt, "(%immediate-bits nil)", OT_NIL);
  check_int(rt, "(%immediate-bits #\\a)", ('a' << 8) | TAG_CHAR);
  check_int(rt, "(%immediate-bits car)", (B_CAR << 8) | TAG_BUILTIN);
  CHECK(eval(rt, "(%immediate-bits \"text\")") == OT_NIL);
  CHECK(eval(rt, "(%immediate-bits 4000000000)") == OT_NIL);
  CHECK(eval(rt, "(%form-offset '(a b))") == OT_NIL);
  ot_destroy(rt);
}

static void check_units(ot_state* rt) {
  check_printed(rt, "(probe const)", "hello");
  check_printed(rt, "out", "hello");
  check_int(rt, "(probe imm)", 42);
  eval(rt, "(probe capture)");
  check_int(rt, "(out 10)", 15);
  eval(rt, "(probe capture-chain)");
  check_int(rt, "((out))", 5);
  eval(rt, "(probe optional)");
  check_int(rt, "(out 1)", 101);
  check_int(rt, "(out 1 2)", 3);
  check_error(rt, "(out)", "with-default");
  check_error(rt, "(out 1 2 3)", "with-default");
  eval(rt, "(probe rest)");
  check_printed(rt, "(out 1 2 3)", "(2 3)");
  check_printed(rt, "(out 1)", "()");
  eval(rt, "(probe call-global)");
  check_int(rt, "out", 42);
  check_printed(rt, "(probe tail-global)", "(1 2)");
  eval(rt, "(probe static)");
  check_int(rt, "(out)", 7);
  eval(rt, "(probe static-nested)");
  /* Each form is a reclamation boundary; the static closure lives on through
   * the code of the closure in out. */
  check_int(rt, "(out)", 9);
  check_int(rt, "(out)", 9);
  eval(rt, "(probe branch)");
  check_int(rt, "(out 3)", 1);
  check_int(rt, "(out 30)", 2);
  eval(rt, "(probe branch-constant)");
  check_int(rt, "(out :yes)", 1);
  check_int(rt, "(out :no)", 2);
  eval(rt, "(probe loop)");
  check_int(rt, "(out 10)", 55);
  check_int(rt, "(out 0)", 0);
  check_printed(rt, "(probe try)", "boom");
}

TEST(optimizer_installs_units) {
  ot_state* rt = units_runtime(false);
  check_units(rt);
  ot_destroy(rt);
}

TEST(optimizer_installs_units_under_gc_stress) {
  ot_state* rt = units_runtime(true);
  check_units(rt);
  ot_destroy(rt);
}

TEST(optimizer_rejects_malformed_units) {
  ot_state* rt = units_runtime(false);

  static const struct {
    const char *unit, *message;
  } cases[] = {
      {"bad-opcode", "opcode is not a fixnum in range"},
      {"bad-register", "word 0 (move): register out of range"},
      {"bad-register-zero", "register 0 holds the running closure"},
      {"bad-jump", "jump target outside the function"},
      {"bad-constant", "constant index out of range"},
      {"bad-closure", "function index out of range"},
      {"bad-global", "unknown global cell"},
      {"bad-immediate", "not an immediate value"},
      {"bad-upvalue", "capture index out of range"},
      {"bad-call-window", "register out of range"},
      {"bad-branch", "needs a jump word after it"},
      {"bad-fall-through", "must not run past its last word"},
      {"bad-entry", "the entry takes no parameters"},
      {"bad-locals", "function 1: locals must cover the parameters"},
      {"bad-optional-rest", "optional and rest parameters cannot combine"},
      {"bad-code-length", "five fixnums per word"},
      {"bad-operand", "operand is not a fixnum in range"},
      {"bad-descriptor", "descriptor must be a table"},
      {"bad-unit", "a non-empty array of functions"},
      {"bad-capture", "captures don't fit this frame"},
      {"bad-static", "a static closure's function has captures"},
      {"bad-static-index", "static constant index out of range"},
      {"bad-points", "points must be in pc order"},
  };

  size_t before = rt->function_count;
  for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
    char source[64];
    snprintf(source, sizeof(source), "(probe %s)", cases[i].unit);
    check_error(rt, source, cases[i].message);
  }
  /* Rejected units leave no functions behind. */
  for (size_t i = before; i < rt->function_count; i++) CHECK(!rt->functions[i]);
  check_printed(rt, "(probe const)", "hello");
  ot_destroy(rt);
}

TEST(optimizer_hook) {
  ot_state* rt = units_runtime(false);
  /* A raising optimizer reports its error; it doesn't fall back. */
  check_error(rt, "(probe boom)", "optimizer boom");
  check_error(rt, "(probe bad-result)", "installed entry function id");
  /* nil falls back to compiler.c, which calls the probe function. */
  check_printed(rt, "(probe 'unknown)", ":fallback");
  /* Evaluation inside the optimizer doesn't reach it. */
  check_printed(rt, "(probe 'nested)", ":fallback");
  check_int(rt, "nested-value", 42);
  check_true(rt, "(seen-form? '(probe 'nested))");
  check_true(rt, "(not (seen-form? '(+ 40 2)))");
  /* The optimizer sees located forms. */
  eval(rt, "   (probe imm)");
  check_int(rt, "last-offset", 3);
  check_error(rt, "(%set-optimizer! 5)", "a procedure or nil");
  eval(rt, "(%set-optimizer! nil)");
  check_printed(rt, "(probe 'const)", ":fallback");
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(optimizer_cells),
    TEST_ENTRY(optimizer_constants),
    TEST_ENTRY(optimizer_installs_units),
    TEST_ENTRY(optimizer_installs_units_under_gc_stress),
    TEST_ENTRY(optimizer_rejects_malformed_units),
    TEST_ENTRY(optimizer_hook),
};

TEST_SUITE(optimizer, tests);
