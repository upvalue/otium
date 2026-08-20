#include "otium.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*test_fn)(void);

typedef struct test_case {
  const char* name;
  test_fn run;
} test_case;

typedef struct capture {
  char data[256];
  size_t length;
} capture;

static int failures;
static int finalized[8];
static size_t finalized_count;
static int module_init_count;

typedef struct interrupt_probe {
  const char* restart;
  int calls;
} interrupt_probe;

static void check(bool condition, const char* message) {
  if (condition) return;
  fprintf(stderr, "FAIL: %s\n", message);
  failures++;
}

static bool evaluate(ots* state, const char* source, otv* out) {
  return ot_eval_src(state, source, strlen(source), "<runtime-test>", out);
}

static ot_interrupt_action invoke_interrupt_restart(ots* state, void* userdata) {
  interrupt_probe* probe = userdata;
  probe->calls++;
  otv ignored = ot_nil;
  const char* source = strcmp(probe->restart, "continue") == 0 ? "(invoke-restart 'continue)"
                                                               : "(invoke-restart 'abort)";
  check(!evaluate(state, source, &ignored), "interrupt restart transfers control");
  return strcmp(probe->restart, "continue") == 0 ? OT_INTERRUPT_ABORT : OT_INTERRUPT_CONTINUE;
}

static ots* test_state(bool stress) {
  ot_config config = ot_config_default();
  config.gc_stress = stress;
  ots* state = ot_create(&config);
  check(state != NULL, "create runtime state");
  return state;
}

static void capture_write(void* userdata, const char* bytes, size_t length) {
  capture* output = userdata;
  size_t available = sizeof output->data - output->length;
  if (length > available) length = available;
  memcpy(output->data + output->length, bytes, length);
  output->length += length;
}

static otv allocating_identity(ots* state, otv* args, int argc) {
  if (argc != 1) return ot_nil;
  for (int i = 0; i < 64; i++) (void)ot_make_string(state, "collection pressure", 19);
  return args[0];
}

static otv module_answer(ots* state, otv* args, int argc) {
  (void)state;
  (void)args;
  return argc == 0 ? ot_make_int(88) : ot_nil;
}

static void init_test_module(ots* state) {
  module_init_count++;
  ot_def_nat(state, "answer", module_answer);
}

static bool load_test_namespace(void* userdata, const char* namespace_name, char** source,
                                size_t* length) {
  int* loads = userdata;
  if (strcmp(namespace_name, "loaded") != 0) return false;
  const char text[] = "(def answer 77)";
  *source = malloc(sizeof text);
  if (*source == NULL) return false;
  memcpy(*source, text, sizeof text);
  *length = sizeof text - 1;
  (*loads)++;
  return true;
}

static void record_finalizer(ots* state, void* payload) {
  (void)state;
  if (finalized_count < sizeof finalized / sizeof finalized[0])
    finalized[finalized_count++] = *(int*)payload;
}

static void test_config_validation(void) {
  ot_config config = ot_config_default();
  check(config.heap_init == 1024u * 1024u, "default initial heap is one MiB");
  check(config.heap_max == 64u * 1024u * 1024u, "default maximum heap is 64 MiB");
  check(config.mailbox_count == 32, "default mailbox count");
  check(config.reductions_per_slice == 1024, "default scheduler reduction slice");
  check(config.max_depth == 200000, "default VM frame depth");
  check(!config.gc_stress, "stress collection is off by default");

  config.heap_init = 512;
  check(ot_create(&config) == NULL, "reject an initial heap below the minimum");
  config = ot_config_default();
  config.heap_max = config.heap_init - 1;
  check(ot_create(&config) == NULL, "reject a maximum below the initial heap");
  config = ot_config_default();
  config.max_depth = 0;
  check(ot_create(&config) == NULL, "reject a zero VM frame depth");
  config = ot_config_default();
  config.mailbox_count = 0;
  check(ot_create(&config) == NULL, "reject a zero mailbox count");
  config = ot_config_default();
  config.reductions_per_slice = 0;
  check(ot_create(&config) == NULL, "reject a zero scheduler reduction slice");
}

static void test_immediate_values(void) {
  check(ot_value_type(ot_nil) == OT_TYPE_NIL, "nil type");
  check(ot_value_type(ot_null) == OT_TYPE_NULL, "empty-list type");
  check(ot_value_type(ot_true) == OT_TYPE_BOOLEAN, "true type");
  check(ot_value_type(ot_false) == OT_TYPE_BOOLEAN, "false type");
  for (intptr_t value = -1000; value <= 1000; value++) {
    otv encoded = ot_make_int(value);
    if (!ot_is_int(encoded) || ot_get_int(encoded) != value) {
      check(false, "integer immediate round trip");
      break;
    }
  }
}

static void test_heap_values(void) {
  ots* state = test_state(true);
  if (state == NULL) return;

  otv string = ot_nil;
  otv symbol = ot_nil;
  otv same_symbol = ot_nil;
  otv keyword = ot_nil;
  otv pair = ot_nil;
  otv array = ot_nil;
  otv table = ot_nil;
  OT_FRAME(state, &string, &symbol, &same_symbol, &keyword, &pair, &array, &table);

  const char raw[] = {'a', '\0', 'b'};
  string = ot_make_string(state, raw, sizeof raw);
  symbol = ot_make_symbol(state, "alpha", 5);
  same_symbol = ot_make_symbol(state, "alpha", 5);
  keyword = ot_make_keyword(state, "alpha", 5);
  check(symbol == same_symbol, "symbols intern per state");
  check(symbol != keyword, "symbols and keywords use separate identities");
  check(ot_value_type(string) == OT_TYPE_STRING, "string type");

  pair = ot_cons(state, string, symbol);
  otv car = ot_nil;
  otv cdr = ot_nil;
  check(ot_pair_values(pair, &car, &cdr) && car == string && cdr == symbol, "pair accessors");
  check(!ot_pair_values(string, &car, &cdr), "pair accessor rejects other types");

  array = ot_array_new(state, 0);
  for (intptr_t i = 0; i < 512; i++) array = ot_array_append(state, array, ot_make_int(i));
  check(ot_array_length(array) == 512, "array growth preserves length");
  check(ot_get_int(ot_array_get(array, 257, ot_nil)) == 257, "array lookup after growth");
  check(ot_array_get(array, 900, ot_false) == ot_false, "array fallback");

  table = ot_table_new(state, 1);
  for (intptr_t i = 0; i < 1024; i++)
    table = ot_table_put(state, table, ot_make_int(i), ot_make_int(i * 3));
  for (intptr_t i = 1; i < 1024; i += 2) table = ot_table_put(state, table, ot_make_int(i), ot_nil);
  check(ot_table_length(table) == 512, "table deletion and compaction length");
  for (intptr_t i = 0; i < 1024; i += 2) {
    otv found = ot_table_get(state, table, ot_make_int(i), ot_nil);
    if (!ot_is_int(found) || ot_get_int(found) != i * 3) {
      check(false, "table lookup after growth and deletion");
      break;
    }
  }

  const char* bytes = NULL;
  size_t length = 0;
  check(ot_string_bytes(string, &bytes, &length) && length == sizeof raw &&
            memcmp(bytes, raw, sizeof raw) == 0,
        "strings preserve embedded zero bytes");

  OT_FRAME_POP(state);
  ot_destroy(state);
}

static void test_float_and_equality(void) {
  ots* state = test_state(true);
  if (state == NULL) return;

  otv one = ot_nil;
  otv another = ot_nil;
  otv nan = ot_nil;
  otv left = ot_nil;
  otv right = ot_nil;
  OT_FRAME(state, &one, &another, &nan, &left, &right);
  one = ot_make_float(state, 1.25);
  another = ot_make_float(state, 1.25);
  nan = ot_make_float(state, NAN);
  double number = 0;
  check(ot_float_value(one, &number) && number == 1.25, "float accessor");
  check(ot_equal(state, one, another, true), "equal boxed floats");
  check(ot_equal(state, nan, nan, true), "NaN value equals itself by identity");

  left = ot_cons(state, ot_make_int(1), ot_cons(state, ot_make_int(2), ot_null));
  right = ot_cons(state, ot_make_int(1), ot_cons(state, ot_make_int(2), ot_null));
  check(ot_equal(state, left, right, true), "structural pair equality");
  check(!ot_equal(state, left, right, false), "pair identity equality remains distinct");

  OT_FRAME_POP(state);
  ot_destroy(state);
}

static void test_reentrant_states(void) {
  ots* left = test_state(true);
  ots* right = test_state(true);
  if (left == NULL || right == NULL) {
    ot_destroy(left);
    ot_destroy(right);
    return;
  }

  otv value = ot_nil;
  check(evaluate(left, "(def counter 10)", &value), "define in first state");
  check(evaluate(right, "(def counter 90)", &value), "define in second state");
  check(evaluate(left, "(set! counter (+ counter 1))", &value) && ot_is_int(value) &&
            ot_get_int(value) == 11,
        "advance first state");
  check(evaluate(right, "(set! counter (+ counter 2))", &value) && ot_is_int(value) &&
            ot_get_int(value) == 92,
        "advance second state");
  check(evaluate(left, "counter", &value) && ot_is_int(value) && ot_get_int(value) == 11,
        "first state remains isolated");

  ot_destroy(left);
  ot_destroy(right);
}

static void test_eval_integration(void) {
  ots* state = test_state(true);
  if (state == NULL) return;
  otv value = ot_nil;
  const char* program = "(define (tail n total) "
                        "  (if (= n 0) total (tail (- n 1) (+ total n)))) "
                        "(tail 50000 0)";
  check(evaluate(state, program, &value) && ot_is_int(value) &&
            ot_get_int(value) == INT64_C(1250025000),
        "deep tail call reuses its VM frame");
  check(evaluate(state, "(fn (x) (if x (+ x 1) 0))", &value), "compile a function");
  const char* bytecode = NULL;
  size_t bytecode_length = 0;
  check(ot_function_bytecode(value, &bytecode, &bytecode_length) && bytecode_length != 0,
        "interpreted functions expose bytecode");
  bool printable = bytecode_length != 0;
  for (size_t i = 0; i < bytecode_length; i++)
    if ((unsigned char)bytecode[i] < 0x20 || (unsigned char)bytecode[i] > 0x7e) printable = false;
  check(printable, "bytecode uses only printable ASCII bytes");
  check(memchr(bytecode, 'B', bytecode_length) != NULL &&
            memchr(bytecode, 'C', bytecode_length) != NULL,
        "compiler separates lexical and published loads");
  check(evaluate(state, "(fn (x) (set! x global-value) (set! global-value x))", &value) &&
            ot_function_bytecode(value, &bytecode, &bytecode_length) &&
            memchr(bytecode, 'Q', bytecode_length) != NULL &&
            memchr(bytecode, 'R', bytecode_length) != NULL,
        "compiler separates lexical and published assignment");
  check(evaluate(state,
                 "(fn (message) "
                 "  (try (error message) (catch (error? e) (condition-message e))))",
                 &value) &&
            ot_function_bytecode(value, &bytecode, &bytecode_length) &&
            memchr(bytecode, '!', bytecode_length) != NULL &&
            memchr(bytecode, 'D', bytecode_length) == NULL &&
            memchr(bytecode, 'I', bytecode_length) == NULL,
        "dynamic forms compile without evaluator escape opcodes");
  check(evaluate(state,
                 "(restart-case (error \"boom\") "
                 "  (use-value (value) value))",
                 &value) == false,
        "unhandled restart case reports a condition");
  check(ot_condition(state) != ot_nil, "failed evaluation stores its condition");
  ot_clear_condition(state);
  check(evaluate(state, "(+ 20 22)", &value) && ot_get_int(value) == 42,
        "state evaluates again after clearing a condition");
  ot_destroy(state);
}

static ot_run_result run_in_slices(ots* state, uint64_t budget, size_t* yields) {
  for (size_t slices = 0; slices < 100000; slices++) {
    ot_run_result result = ot_run(state, budget);
    if (result.status != OT_RUN_YIELDED) return result;
    (*yields)++;
    ot_collect(state);
  }
  check(false, "sliced root process eventually finishes");
  return (ot_run_result){.status = OT_RUN_FAILED, .value = ot_nil};
}

static void test_yield_resume(void) {
  ots* state = test_state(true);
  if (state == NULL) return;

  otv function = ot_nil;
  otv argument = ot_make_int(1000);
  OT_FRAME(state, &function, &argument);
  check(evaluate(state,
                 "(fn (n) "
                 "  (let loop ((n n) (total 0)) "
                 "    (if (= n 0) total (loop (- n 1) (+ total n)))))",
                 &function),
        "compile resumable root function");
  check(ot_start_call(state, function, &argument, 1), "start root process call");
  size_t yields = 0;
  ot_run_result result = run_in_slices(state, 7, &yields);
  check(result.status == OT_RUN_COMPLETED && ot_is_int(result.value) &&
            ot_get_int(result.value) == 500500,
        "root process resumes to the uninterrupted result");
  check(yields > 100, "small reduction budget yields repeatedly");

  check(evaluate(state,
                 "(defparam sliced-depth 0) "
                 "(fn (n) "
                 "  (with-params ((sliced-depth 9)) "
                 "    (let loop ((n n)) "
                 "      (if (= n 0) (sliced-depth) (loop (- n 1))))))",
                 &function),
        "compile dynamic-extent root function");
  argument = ot_make_int(200);
  check(ot_start_call(state, function, &argument, 1), "start dynamic-extent root call");
  yields = 0;
  result = run_in_slices(state, 1, &yields);
  check(result.status == OT_RUN_COMPLETED && ot_is_int(result.value) &&
            ot_get_int(result.value) == 9,
        "safe-boundary yielding preserves dynamic parameter state");
  check(yields != 0, "dynamic-extent call yields at safe boundaries");

  check(evaluate(state,
                 "(fn (n) "
                 "  (try "
                 "    (let loop ((n n)) "
                 "      (if (= n 0) (error \"caught after slices\") (loop (- n 1)))) "
                 "    (catch (error? condition) 41)))",
                 &function),
        "compile resumable condition handler");
  argument = ot_make_int(200);
  check(ot_start_call(state, function, &argument, 1), "start resumable condition handler");
  yields = 0;
  result = run_in_slices(state, 1, &yields);
  check(result.status == OT_RUN_COMPLETED && ot_is_int(result.value) &&
            ot_get_int(result.value) == 41,
        "try continuation catches a condition after resumptions");
  check(yields > 100, "try body yields while its continuation is suspended");

  check(evaluate(state,
                 "(fn (n) "
                 "  (let ((cleaned 0)) "
                 "    (let ((answer "
                 "      (handler-bind "
                 "        (((type-pred 'error) "
                 "          (fn (condition) (invoke-restart 'use-value 40)))) "
                 "        (restart-case "
                 "          (unwind-protect "
                 "            (let loop ((n n)) "
                 "              (if (= n 0) (error \"restart after slices\") "
                 "                  (loop (- n 1)))) "
                 "            (set! cleaned (+ cleaned 1))) "
                 "          (use-value (value) (+ value cleaned)))))) "
                 "      (+ answer cleaned))))",
                 &function),
        "compile nested dynamic continuations");
  argument = ot_make_int(200);
  check(ot_start_call(state, function, &argument, 1), "start nested dynamic continuations");
  yields = 0;
  result = run_in_slices(state, 1, &yields);
  check(result.status == OT_RUN_COMPLETED && ot_is_int(result.value) &&
            ot_get_int(result.value) == 42,
        "handlers, restarts, and cleanup resume in the right order");
  check(yields > 100, "nested dynamic continuation bodies yield repeatedly");

  check(evaluate(state,
                 "(def sliced-interrupt-cleaned 0) "
                 "(fn (n) "
                 "  (unwind-protect "
                 "    (let loop ((n n)) (if (= n 0) n (loop (- n 1)))) "
                 "    (set! sliced-interrupt-cleaned 1)))",
                 &function),
        "compile interruptible cleanup function");
  argument = ot_make_int(5000);
  check(ot_start_call(state, function, &argument, 1), "start interruptible root call");
  result = ot_run(state, 7);
  check(result.status == OT_RUN_YIELDED, "root call yields before interruption");
  ot_interrupt(state);
  yields = 1;
  result = run_in_slices(state, 7, &yields);
  check(result.status == OT_RUN_FAILED, "interrupt fails the resumed root call");
  ot_clear_condition(state);
  check(evaluate(state, "sliced-interrupt-cleaned", &argument) && ot_is_int(argument) &&
            ot_get_int(argument) == 1,
        "interrupt unwind runs suspended cleanup code");

  check(evaluate(state,
                 "(fn (n) "
                 "  (let loop ((n n)) "
                 "    (if (= n 0) (error \"sliced failure\") (loop (- n 1)))))",
                 &function),
        "compile failing resumable root function");
  argument = ot_make_int(100);
  check(ot_start_call(state, function, &argument, 1), "start failing root call");
  yields = 0;
  result = run_in_slices(state, 3, &yields);
  check(result.status == OT_RUN_FAILED && result.value == ot_condition(state),
        "failed root process returns its condition");
  check(yields != 0, "failing root process resumes before failure");
  ot_clear_condition(state);

  OT_FRAME_POP(state);
  ot_destroy(state);
}

static bool condition_message_is(ots* state, const char* expected) {
  otv key = ot_make_keyword(state, "message", 7);
  otv condition = ot_condition(state);
  otv message = ot_table_get(state, condition, key, ot_nil);
  const char* bytes = NULL;
  size_t length = 0;
  size_t expected_length = strlen(expected);
  return ot_string_bytes(message, &bytes, &length) && length == expected_length &&
         memcmp(bytes, expected, length) == 0;
}

static void test_named_let(void) {
  ot_config config = ot_config_default();
  config.gc_stress = true;
  config.max_depth = 64;
  ots* state = ot_create(&config);
  check(state != NULL, "create named-let runtime state");
  if (state == NULL) return;
  otv value = ot_nil;

  check(evaluate(state, "(let loop () #t)", &value) && value == ot_true,
        "named let accepts no bindings");
  check(evaluate(state,
                 "(let sum ((n 50000) (total 0)) "
                 "  (if (= n 0) total (sum (- n 1) (+ total n))))",
                 &value) &&
            ot_is_int(value) && ot_get_int(value) == INT64_C(1250025000),
        "named let recursion reuses its VM frame");
  check(evaluate(state,
                 "(let ((x 10)) "
                 "  (let loop ((x 1) (outer-x x)) outer-x))",
                 &value) &&
            ot_is_int(value) && ot_get_int(value) == 10,
        "named let initializers use the enclosing scope");
  check(evaluate(state, "(let loop (broken) #t)", &value) == false &&
            condition_message_is(state, "let: invalid binding"),
        "named let rejects an invalid binding");
  ot_clear_condition(state);
  check(evaluate(state, "(let loop 42 #t)", &value) == false &&
            condition_message_is(state, "let: bad bindings"),
        "named let rejects a non-list binding set");
  ot_clear_condition(state);
  check(evaluate(state, "(let 42 () #t)", &value) == false &&
            condition_message_is(state, "let: bad bindings"),
        "let rejects a non-symbol named-let name");
  ot_clear_condition(state);
  check(evaluate(state, "(let loop ((value missing)) value)", &value) == false &&
            condition_message_is(state, "unbound symbol: missing"),
        "named let propagates initializer errors");
  ot_clear_condition(state);
  check(evaluate(state,
                 "(define (deep n) (if (= n 0) 0 (+ 1 (deep (- n 1))))) "
                 "(deep 100)",
                 &value) == false &&
            condition_message_is(state, "maximum VM frame depth exceeded"),
        "non-tail recursion observes the VM frame limit");
  ot_clear_condition(state);
  check(evaluate(state, "deep", &value), "load deep function for sliced execution");
  otv depth = ot_make_int(100);
  check(ot_start_call(state, value, &depth, 1), "start sliced frame-limit call");
  size_t yields = 0;
  ot_run_result result = run_in_slices(state, 3, &yields);
  check(result.status == OT_RUN_FAILED &&
            condition_message_is(state, "maximum VM frame depth exceeded"),
        "resumed non-tail recursion observes the VM frame limit");
  check(yields != 0, "non-tail recursion yields before reaching its frame limit");
  ot_clear_condition(state);
  check(evaluate(state, "(+ 20 22)", &value) && ot_get_int(value) == 42,
        "VM runs again after a frame-limit unwind");

  ot_destroy(state);
}

static void test_try_ast_reuse(void) {
  ots* state = test_state(true);
  if (state == NULL) return;
  otv value = ot_nil;
  const char* program = "(define (recover message) "
                        "  (try (+ 1 2) (error message) "
                        "    (catch (error? e) (condition-message e)))) "
                        "(recover \"first\") "
                        "(recover \"second\")";
  const char* bytes = NULL;
  size_t length = 0;
  check(evaluate(state, program, &value) && ot_string_bytes(value, &bytes, &length) &&
            length == 6 && memcmp(bytes, "second", length) == 0,
        "try retains catch clauses across function calls");
  ot_destroy(state);
}

static void test_writer_callback(void) {
  ots* state = test_state(false);
  if (state == NULL) return;
  capture output = {0};
  ot_set_writer(state, capture_write, &output);
  otv value = ot_nil;
  check(evaluate(state, "(println \"answer\" 42)", &value), "evaluate output call");
  check(output.length == 10 && memcmp(output.data, "answer 42\n", 10) == 0,
        "writer receives exact output bytes");
  ot_set_writer(state, NULL, NULL);
  ot_destroy(state);
}

static void test_modules_and_loader(void) {
  ots* state = test_state(true);
  if (state == NULL) return;
  module_init_count = 0;
  int loads = 0;
  ot_register_module(state, "cmod", init_test_module);
  ot_set_loader(state, load_test_namespace, &loads);
  otv value = ot_nil;
  check(evaluate(state, "(require 'cmod) (cmod/answer)", &value) && ot_is_int(value) &&
            ot_get_int(value) == 88,
        "registered C module loads through require");
  check(evaluate(state, "(require 'cmod) (cmod/answer)", &value) && module_init_count == 1,
        "module initializer runs once");
  check(evaluate(state, "(require 'loaded) loaded/answer", &value) && ot_is_int(value) &&
            ot_get_int(value) == 77,
        "loader source defines a namespace value");
  check(loads == 1, "source loader runs once");
  ot_destroy(state);
}

static void test_roots_and_stats(void) {
  ots* state = test_state(true);
  if (state == NULL) return;

  ot_def_nat(state, "allocating-identity", allocating_identity);
  otv value = ot_nil;
  const char* bytes = NULL;
  size_t length = 0;
  check(evaluate(state, "(allocating-identity \"still rooted\")", &value),
        "call allocating native under GC stress");
  check(ot_string_bytes(value, &bytes, &length) && length == 12 &&
            memcmp(bytes, "still rooted", length) == 0,
        "native argument survives moving collections");

  otv pinned = ot_make_string(state, "global root", 11);
  OT_GLOBAL(state, &pinned);
  for (int i = 0; i < 64; i++) (void)ot_make_string(state, "more pressure", 13);
  check(ot_string_bytes(pinned, &bytes, &length) && length == 11 &&
            memcmp(bytes, "global root", length) == 0,
        "registered host root survives moving collections");
  ot_gc_stats before = ot_get_gc_stats(state);
  ot_collect(state);
  ot_gc_stats after = ot_get_gc_stats(state);
  check(after.collections == before.collections + 1, "explicit collection updates stats");
  check(after.allocations >= before.allocations, "allocation counter is monotonic");

  ot_destroy(state);
}

static void test_extension_values(void) {
  finalized_count = 0;
  ots* state = test_state(false);
  if (state == NULL) return;

  unsigned inline_type = ot_ext_type(state, "test/inline", NULL);
  unsigned owned_type = ot_ext_type(state, "test/owned", record_finalizer);
  int inline_value = 41;
  otv inline_ext = ot_ext_inline(state, inline_type, &inline_value, sizeof inline_value);
  OT_FRAME(state, &inline_ext);
  void* payload = NULL;
  check(ot_ext_check(state, "test", inline_ext, inline_type, &payload) && *(int*)payload == 41,
        "inline extension payload");
  check(!ot_ext_check(state, "test", inline_ext, owned_type, &payload),
        "extension type mismatch raises a condition");
  check(ot_condition(state) != ot_nil, "extension type error stores a condition");
  ot_clear_condition(state);

  int first = 1;
  int second = 2;
  int released = 3;
  (void)ot_ext_pointer(state, owned_type, &first);
  (void)ot_ext_pointer(state, owned_type, &second);
  ot_collect(state);
  check(finalized_count == 2 && finalized[0] == 2 && finalized[1] == 1,
        "unreachable extension values finalize in side-list order");

  otv value = ot_ext_pointer(state, owned_type, &released);
  check(ot_ext_release(state, "test", value, owned_type) == value,
        "explicit extension release succeeds");
  ot_collect(state);
  check(finalized_count == 3 && finalized[2] == 3,
        "released extension value finalizes exactly once");

  OT_FRAME_POP(state);
  ot_destroy(state);
  check(finalized_count == 3, "state destruction does not repeat finalizers");
}

static void test_interrupt(void) {
  ots* state = test_state(true);
  if (state == NULL) return;
  otv value = ot_nil;
  check(evaluate(state, "(define (spin n) (if (= n 0) n (spin (- n 1))))", &value),
        "define interrupt test loop");
  ot_interrupt(state);
  check(!evaluate(state, "(spin 5000)", &value),
        "pending interrupt stops evaluation at a poll point");
  ot_clear_condition(state);

  interrupt_probe probe = {.restart = "continue"};
  ot_set_interrupt_hook(state, invoke_interrupt_restart, &probe);
  ot_interrupt(state);
  check(evaluate(state, "(spin 5000)", &value) && ot_is_int(value) && ot_get_int(value) == 0,
        "continue restart resumes interrupted evaluation");
  check(probe.calls == 1, "interrupt hook runs once for continue");

  probe.restart = "abort";
  ot_interrupt(state);
  check(!evaluate(state, "(spin 5000)", &value), "abort restart stops interrupted evaluation");
  check(probe.calls == 2, "interrupt hook runs once for abort");
  ot_clear_condition(state);
  check(evaluate(state, "(+ 20 22)", &value) && ot_get_int(value) == 42,
        "state evaluates again after interrupt abort");
  ot_destroy(state);
}

static void test_concurrency(void) {
  ot_config config = ot_config_default();
  config.gc_stress = true;
  config.mailbox_count = 1;
  config.reductions_per_slice = 3;
  ots* state = ot_create(&config);
  check(state != NULL, "create concurrency test state");
  if (state == NULL) return;
  capture output = {0};
  ot_set_writer(state, capture_write, &output);
  otv value = ot_nil;
  OT_FRAME(state, &value);

  check(evaluate(state,
                 "(define (take-one) (println (receive))) "
                 "(define target (spawn take-one)) "
                 "(println (send target 1)) "
                 "(println (send target 2))",
                 &value),
        "run bounded mailbox processes");
  check(evaluate(state, "(alive? target)", &value) && value == ot_false,
        "completed process is no longer alive");
  check(evaluate(state, "(eq? (send target 3) :dead)", &value) && value == ot_true,
        "sending to an exited process reports dead");

  check(evaluate(state, "(define blocked (spawn take-one))", &value),
        "leave a process blocked on receive");
  check(evaluate(state, "(eq? (send blocked {}) :not-sendable)", &value) && value == ot_true,
        "reject unsupported mutable message values");
  check(evaluate(state, "(send! blocked [7 8])", &value) && ot_value_type(value) == OT_TYPE_ARRAY &&
            ot_array_length(value) == 2 && ot_get_int(ot_array_get(value, 0, ot_nil)) == 7,
        "wake a blocked process with a copied array");
  check(evaluate(state, "(alive? blocked)", &value) && value == ot_false,
        "woken receiver runs to completion");

  check(evaluate(state, "(define (wait-tail) (receive)) (define tailer (spawn wait-tail))", &value),
        "block receive in tail position");
  check(evaluate(state, "(send! tailer '(done))", &value), "resume tail-position receive");
  check(evaluate(state, "(alive? tailer)", &value) && value == ot_false,
        "tail-position receive returns and exits");

  check(evaluate(state,
                 "(define cyclic (spawn wait-tail)) "
                 "(define cycle [nil]) "
                 "(put! cycle 0 cycle) "
                 "(send! cyclic cycle)",
                 &value) &&
            ot_array_get(value, 0, ot_nil) == value,
        "copy a cyclic mutable message graph");
  check(evaluate(state, "(alive? cyclic)", &value) && value == ot_false,
        "cyclic-message receiver exits normally");

  check(evaluate(state, "(define (boom) (error \"child failed\")) (spawn boom)", &value),
        "unhandled child condition does not fail the root evaluation");
  check(evaluate(state, "(+ 20 22)", &value) && ot_get_int(value) == 42,
        "runtime remains usable after child failure");

  const char expected[] = ":ok\n:full\n1\n[7 8]\n";
  check(output.length == sizeof expected - 1 &&
            memcmp(output.data, expected, sizeof expected - 1) == 0,
        "scheduler output shows bounded FIFO delivery");
  OT_FRAME_POP(state);
  ot_destroy(state);
}

static const test_case tests[] = {
    {"config", test_config_validation},          {"immediates", test_immediate_values},
    {"heap-values", test_heap_values},           {"float-equality", test_float_and_equality},
    {"reentrant", test_reentrant_states},        {"eval", test_eval_integration},
    {"yield-resume", test_yield_resume},         {"named-let", test_named_let},
    {"try-ast-reuse", test_try_ast_reuse},       {"writer", test_writer_callback},
    {"modules-loader", test_modules_and_loader}, {"roots-stats", test_roots_and_stats},
    {"extensions", test_extension_values},       {"interrupt", test_interrupt},
    {"concurrency", test_concurrency},
};

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : NULL;
  size_t ran = 0;
  for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
    if (filter != NULL && strstr(tests[i].name, filter) == NULL) continue;
    int before = failures;
    tests[i].run();
    ran++;
    if (failures == before) fprintf(stderr, "ok %s\n", tests[i].name);
  }
  if (ran == 0) {
    fprintf(stderr, "no runtime tests matched: %s\n", filter);
    return 2;
  }
  if (failures != 0) {
    fprintf(stderr, "%d failure(s) in %zu runtime test(s)\n", failures, ran);
    return 1;
  }
  fprintf(stderr, "%zu runtime test(s) passed\n", ran);
  return 0;
}
