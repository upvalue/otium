#include "otium.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef OT_HEAP_INIT
#define OT_HEAP_INIT (1024u * 1024u)
#endif
#ifndef OT_HEAP_MAX
#define OT_HEAP_MAX (64u * 1024u * 1024u)
#endif
#ifndef OT_MAX_DEPTH
#define OT_MAX_DEPTH 200000u
#endif

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

static otv trigger_interrupt(ots* state, otv* args, int argc) {
  (void)args;
  if (argc != 0) return ot_nil;
  ot_interrupt(state);
  return ot_nil;
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
  check(config.heap_init == (size_t)OT_HEAP_INIT, "configured initial heap default");
  check(config.heap_max == (size_t)OT_HEAP_MAX, "configured maximum heap default");
  check(config.max_depth == (unsigned)OT_MAX_DEPTH, "configured VM frame depth default");
  check(!config.gc_stress, "stress collection is off by default");
  check(!config.gc_force_compact, "forced compaction is off by default");

  config.heap_init = 512;
  check(ot_create(&config) == NULL, "reject an initial heap below the minimum");
  config = ot_config_default();
  config.heap_max = config.heap_init - 1;
  check(ot_create(&config) == NULL, "reject a maximum below the initial heap");
  config = ot_config_default();
  config.max_depth = 0;
  check(ot_create(&config) == NULL, "reject a zero VM frame depth");
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

static void test_dynamic_vm(void) {
  ots* state = test_state(true);
  if (state == NULL) return;
  otv value = ot_nil;
  ot_def_nat(state, "trigger-interrupt", trigger_interrupt);

  check(evaluate(state,
                 "(defparam dynamic-depth 0) "
                 "(with-params ((dynamic-depth 9)) "
                 "  (let loop ((n 200)) "
                 "    (if (= n 0) (dynamic-depth) (loop (- n 1)))))",
                 &value) &&
            ot_is_int(value) && ot_get_int(value) == 9,
        "dynamic parameters span synchronous VM calls");

  check(evaluate(state,
                 "(try "
                 "  (let loop ((n 200)) "
                 "    (if (= n 0) (error \"caught in bytecode\") (loop (- n 1)))) "
                 "  (catch (error? condition) 41))",
                 &value) &&
            ot_is_int(value) && ot_get_int(value) == 41,
        "bytecode condition handlers catch nested VM failures");

  check(evaluate(state,
                 "(let ((cleaned 0)) "
                 "  (let ((answer "
                 "    (handler-bind "
                 "      (((type-pred 'error) "
                 "        (fn (condition) (invoke-restart 'use-value 40)))) "
                 "      (restart-case "
                 "        (unwind-protect "
                 "          (let loop ((n 200)) "
                 "            (if (= n 0) (error \"restart in bytecode\") "
                 "                (loop (- n 1)))) "
                 "          (set! cleaned (+ cleaned 1))) "
                 "        (use-value (value) (+ value cleaned)))))) "
                 "    (+ answer cleaned)))",
                 &value) &&
            ot_is_int(value) && ot_get_int(value) == 42,
        "handlers, restarts, and cleanup preserve their dynamic order");

  check(evaluate(state, "(def interrupt-cleaned 0)", &value), "define interrupt cleanup marker");
  check(!evaluate(state,
                  "(unwind-protect "
                  "  (begin (trigger-interrupt) "
                  "    (let loop ((n 5000)) (if (= n 0) n (loop (- n 1))))) "
                  "  (set! interrupt-cleaned 1))",
                  &value),
        "interrupt aborts synchronous bytecode");
  ot_clear_condition(state);
  check(evaluate(state, "interrupt-cleaned", &value) && ot_is_int(value) && ot_get_int(value) == 1,
        "interrupt unwind runs bytecode cleanup");

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
  check(after.collections > before.collections, "explicit collection updates stats");
  check(after.mutator_pause.collections == before.mutator_pause.collections + 1,
        "explicit collection records one inclusive mutator pause");
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

static void test_gc_layout(void) {
  ot_config config = ot_config_default();
  config.gc_stress = true;
  config.gc_force_compact = true;
  ots* state = ot_create(&config);
  check(state != NULL, "create forced-compaction state");
  if (state == NULL) return;

  otv keep = ot_nil;
  otv discard = ot_nil;
  otv value = ot_nil;
  otv wide = ot_nil;
  OT_FRAME(state, &keep, &discard, &value, &wide);
  keep = ot_array_new(state, 40000);
  discard = ot_array_new(state, 40000);

  size_t wide_length = 1280u * 1024u;
  char* wide_source = malloc(wide_length);
  check(wide_source != NULL, "allocate multi-chunk string source");
  if (wide_source != NULL) {
    memset(wide_source, 'x', wide_length);
    wide_source[wide_length / 2] = 'y';
    wide = ot_make_string(state, wide_source, wide_length);
    free(wide_source);
  }
  for (int i = 0; i < 256; i++) {
    value = ot_make_string(state, "kept", 4);
    keep = ot_array_append(state, keep, value);
    value = ot_make_string(state, "discarded", 9);
    discard = ot_array_append(state, discard, value);
  }

  ot_collect(state);
  discard = ot_nil;
  ot_gc_stats before = ot_get_gc_stats(state);
  ot_collect(state);
  ot_gc_stats after = ot_get_gc_stats(state);

  check(ot_array_length(keep) == 256, "large multi-card slots object survives compaction");
  const char* bytes = NULL;
  size_t length = 0;
  value = ot_array_get(keep, 173, ot_nil);
  check(ot_string_bytes(value, &bytes, &length) && length == 4 && memcmp(bytes, "kept", 4) == 0,
        "old-to-young slots retain values across forced compaction");
  check(ot_string_bytes(wide, &bytes, &length) && length == wide_length && bytes[0] == 'x' &&
            bytes[wide_length / 2] == 'y' && bytes[wide_length - 1] == 'x',
        "contiguous byte object survives cards, chunks, and compaction");
  check(after.collections > before.collections, "layout test performs an explicit collection");
#ifdef OT_GC_GEN
  check(after.major_compact.collections > before.major_compact.collections,
        "forced major collection records a compaction");
#if OT_GC_MARK_STACK_ENTRIES <= 8
  check(after.mark_stack_overflows > 0, "bounded mark stack uses its overflow path");
#endif
#endif

  OT_FRAME_POP(state);
  ot_destroy(state);
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

static const test_case tests[] = {
    {"config", test_config_validation},
    {"immediates", test_immediate_values},
    {"heap-values", test_heap_values},
    {"float-equality", test_float_and_equality},
    {"reentrant", test_reentrant_states},
    {"eval", test_eval_integration},
    {"dynamic-vm", test_dynamic_vm},
    {"named-let", test_named_let},
    {"try-ast-reuse", test_try_ast_reuse},
    {"writer", test_writer_callback},
    {"modules-loader", test_modules_and_loader},
    {"roots-stats", test_roots_and_stats},
    {"extensions", test_extension_values},
    {"gc-layout", test_gc_layout},
    {"interrupt", test_interrupt},
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
