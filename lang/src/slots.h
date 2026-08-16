// slots.h — how this codebase touches heap values. Outside the few files that
// work on heap internals directly (heap.c, vm.c, slots.c, collections.c),
// this API is the only way to reach the GC heap, and the classic mistake
// cannot be written in it.
//
// The collector is a moving semispace scavenger, so a raw pointer or Value
// naming a heap object goes stale at the next allocation. Here:
//
//   - Heap values live in rooted stack slots, named by Ref. Operations take
//     Refs and write results into a caller-provided destination Ref (aliasing
//     an operand is fine). Nothing here returns a pointer into the GC heap.
//   - C scalars (i64, f64, intern ids, bytes copied into a C-heap Buf) cross
//     the boundary freely; they cannot go stale.
//   - Value appears only as an opaque carrier: immediates you construct
//     (int_v, bool_v, ...), the OT_TRY/raise_error plumbing, and ot_ret in
//     return position. No accessor for its heap side exists in this header.
//
// Builtins and extensions include this header (plus builtins.h) and nothing
// else from src/. Runtime files may add state.h. heap.h fails to compile
// outside its allowed includers; tests/check_hygiene.py keeps that list.
#pragma once
#include "common.h"
#include "vec.h"
#include "value.h"

typedef struct State State;

// --- rooted slots -----------------------------------------------------------
//
// A Ref names a slot on the VM's value stack, the GC root set. Slots survive
// collection; the values in them are updated when objects move. Deliberately a
// struct so it cannot be confused with an index or a Value.
typedef struct Ref {
  u32 i;
} Ref;

typedef enum Status : u8 {
  Status_Ok,
  Status_Unwind,
} Status;

// OT_SCOPE(vm) opens a stack region that every exit path unwinds. One per
// function; a region that wants its own scope becomes its own function.
typedef struct ScopeGuard {
  State* vm;  // null once disarmed
  u32 base;
} ScopeGuard;
void ot_scope_release(ScopeGuard* g);
ScopeGuard ot_scope_open(State* vm);
#define OT_SCOPE(vm)                                                                               \
  [[maybe_unused]] ScopeGuard _otScope __attribute__((cleanup(ot_scope_release))) = ot_scope_open(vm)
// Propagate a callee's unwind. The enclosing OT_SCOPE does the popping.
#define OT_CHECK(expr)                                                                             \
  do {                                                                                             \
    if ((expr) != Status_Ok) return Status_Unwind;                                                 \
  } while (0)

Ref ot_push(State* vm);                // push a nil scratch slot
Ref ot_push_copy(State* vm, Ref src);  // push a copy of a slot
Ref ot_push_im(State* vm, Value immediate);  // push an immediate (never a heap value)
void ot_copy(State* vm, Ref dst, Ref src);
// Root a Value returned by a just-completed callback before any further call.
void ot_set_return(State* vm, Ref dst, Value returned);
u32 ot_top(State* vm);                 // current stack height (an argBase for ot_apply)
void ot_pop_to(State* vm, u32 top);    // drop slots above a height taken from ot_top

// Read a slot for immediate return from a native. Return position only: the
// result must flow straight out, never into a local that lives across a call.
Value ot_ret(State* vm, Ref r);

// --- scalars in and out -----------------------------------------------------

Tag ot_tag(State* vm, Ref r);
bool ot_nil(State* vm, Ref r);
bool ot_truthy(State* vm, Ref r);
i64 ot_int(State* vm, Ref r);    // Tag_Int only
f64 ot_float(State* vm, Ref r);  // Tag_Float only
f64 ot_num(State* vm, Ref r);    // int or float, as f64
u32 ot_id(State* vm, Ref r);     // symbol/keyword intern id

void ot_set_nil(State* vm, Ref r);
void ot_set_null(State* vm, Ref r);
void ot_set_bool(State* vm, Ref r, bool b);
void ot_set_int(State* vm, Ref r, i64 i);
void ot_set_float(State* vm, Ref r, f64 f);
void ot_set_symbol(State* vm, Ref r, u32 id);
void ot_set_keyword(State* vm, Ref r, u32 id);

typedef enum OtNumCompare : u8 {
  OtNumCompare_Eq,
  OtNumCompare_Lt,
  OtNumCompare_Gt,
  OtNumCompare_Le,
  OtNumCompare_Ge,
} OtNumCompare;
Value ot_num_add_args(State* vm, u32 base, u32 argc);
Value ot_num_mul_args(State* vm, u32 base, u32 argc);
Value ot_num_sub_args(State* vm, u32 base, u32 argc);
Value ot_num_compare_args(State* vm, u32 base, u32 argc, const char* who, OtNumCompare op);

bool ot_eq(State* vm, Ref a, Ref b);     // eq? (identity)
bool ot_equal(State* vm, Ref a, Ref b);  // equal? (structural)

// --- constructors (result into dst) -----------------------------------------

void ot_make_array(State* vm, Ref dst, u32 cap);
void ot_make_table(State* vm, Ref dst);
void ot_make_buffer(State* vm, Ref dst);
// `bytes` must not point into the GC heap (C-heap or static only).
void ot_make_string(State* vm, Ref dst, const char* bytes, u32 len);
void ot_make_string_buf(State* vm, Ref dst, const Buf* b);
void ot_cons(State* vm, Ref dst, Ref car, Ref cdr);

// --- pairs ------------------------------------------------------------------

void ot_car(State* vm, Ref dst, Ref pair);
void ot_cdr(State* vm, Ref dst, Ref pair);
// Build a list from `n` values in consecutive stack slots at `base`, folding
// right to left onto `tail`, into dst (dst outside [base, base+n)).
void ot_list_from_stack(State* vm, Ref dst, u32 base, u32 n, Ref tail);
// Raw field write; the checks below are the caller's job where the language
// requires them.
void ot_pair_set(State* vm, Ref pair, bool car, Ref v);
bool ot_pair_key_frozen(State* vm, Ref pair);       // used as a table key
bool ot_pair_contains(State* vm, Ref root, Ref needle);  // pair-graph reachability

// --- arrays -----------------------------------------------------------------

u32 ot_array_len(State* vm, Ref arr);
void ot_array_get(State* vm, Ref dst, Ref arr, i64 i);  // nil when out of range
bool ot_array_set(State* vm, Ref arr, i64 i, Ref v);    // false when out of range
void ot_array_push(State* vm, Ref arr, Ref v);
void ot_array_push_im(State* vm, Ref arr, Value imm);  // immediates only
void ot_array_pop(State* vm, Ref dst, Ref arr);        // nil when empty

// --- tables -----------------------------------------------------------------
//
// The _im variants take immediate keys/values (ints, symbols, keywords, ...)
// as raw Values — immediates cannot go stale — and refuse heap values.

u32 ot_table_count(State* vm, Ref t);
void ot_table_get(State* vm, Ref dst, Ref t, Ref key);  // nil on miss
void ot_table_get_im(State* vm, Ref dst, Ref t, Value immKey);
void ot_table_put(State* vm, Ref t, Ref key, Ref v);  // nil value deletes
void ot_table_put_im(State* vm, Ref t, Value immKey, Ref v);
void ot_table_put_im2(State* vm, Ref t, Value immKey, Value immVal);
// Insertion-ordered iteration; *cursor starts at 0. Writes the next live
// entry into k/v and returns true, or returns false at the end. Mutating the
// table between steps follows the same rules as today's builtins: entries
// added behind the cursor are not revisited.
bool ot_table_next(State* vm, Ref t, u32* cursor, Ref k, Ref v);

// Combined language collection primitives. These keep the hot get/put/push!
// paths behind one boundary call without exposing collection storage.
Value ot_collection_get(State* vm, Ref dst, Ref collection, Ref key, Ref dflt);
Value ot_collection_put(State* vm, Ref collection, Ref key, Ref value);
Value ot_array_push_args(State* vm, Ref array, u32 base, u32 count);

// --- strings ----------------------------------------------------------------
//
// Positions here are BYTE offsets into the UTF-8 storage; use
// ot_string_utf8_offset to convert a code-point index. Byte reads and
// comparisons are implemented core-side so no storage pointer crosses over.

u32 ot_string_len(State* vm, Ref s);     // bytes
u32 ot_string_nchars(State* vm, Ref s);  // code points
u8 ot_string_byte(State* vm, Ref s, u32 i);
u32 ot_string_utf8_offset(State* vm, Ref s, u32 nth);  // clamped to [0, len]
void ot_substring(State* vm, Ref dst, Ref s, u32 byteOff, u32 byteLen);
void ot_string_copy(State* vm, Ref s, u32 byteOff, u32 byteLen, Buf* out);  // append to out
int ot_string_cmp(State* vm, Ref a, Ref b);  // lexicographic byte order
bool ot_string_region_eq(State* vm, Ref a, u32 aOff, Ref b, u32 bOff, u32 len);
bool ot_string_find(State* vm, Ref hay, Ref needle, u32 fromByte, u32* atByte);

// --- buffers ----------------------------------------------------------------

u32 ot_buffer_len(State* vm, Ref b);     // bytes
u32 ot_buffer_nchars(State* vm, Ref b);  // code points
void ot_buffer_copy(State* vm, Ref b, Buf* out);  // append all bytes to out
// `src` must not point into the GC heap (C-heap or static only).
void ot_buffer_append(State* vm, Ref b, const char* src, u32 n);
void ot_buffer_to_string(State* vm, Ref dst, Ref b);

// --- interning and names ----------------------------------------------------

u32 ot_intern(State* vm, const char* s, u32 len);
const char* ot_intern_name(State* vm, u32 id, u32* lenOut);  // C-heap, stable
u32 ot_name_id(State* vm, Ref v);  // symbol/keyword/string -> id, else 0

// --- printing and output ----------------------------------------------------

void ot_display(State* vm, Ref v, Buf* out);
void ot_repr(State* vm, Ref v, Buf* out);
void ot_write_out(State* vm, const char* s, u32 n);  // the host's write seam

// Opaque-object metadata used by diagnostics and the printer.
u32 ot_callable_name(State* vm, Ref fn);
bool ot_callable_code(State* vm, Ref dst, Ref fn);
u32 ot_param_name(State* vm, Ref param);
u32 ot_foreign_name(State* vm, Ref foreign);  // intern id, or 0
void ot_code_ascii(State* vm, Ref code, Buf* out);

// --- sequence iteration ------------------------------------------------------
//
// Rooted iteration over lists and arrays. The caller owns both slots for the
// iterator's lifetime; keeping the cursor and current item on the VM stack
// makes them safe across a moving collection.

typedef enum SeqStep : u8 {
  SeqStep_Item,
  SeqStep_End,
  SeqStep_Improper,
  SeqStep_NotSequence,
} SeqStep;

typedef enum SeqKind : u8 {
  SeqKind_Invalid,
  SeqKind_Empty,
  SeqKind_List,
  SeqKind_Array,
} SeqKind;

typedef struct SeqIter {
  State* vm;
  Ref cursor;
  u32 index;
  u32 limit;
  SeqKind kind;
} SeqIter;

void seq_iter_init(SeqIter* it, State* vm, Ref rootedSequence);
SeqStep seq_iter_next(SeqIter* it, Ref item);
Value sequence_error(State* vm, const char* who, SeqStep step);

// --- native functions --------------------------------------------------------

// Calling convention: arguments live at stack[base .. base+argc); the return
// Value is either an immediate you constructed, ot_ret of a slot, or the
// unwind sentinel from raise_error/OT_TRY.
typedef Value (*NativeFn)(State* vm, u32 base, u32 argc);
void ot_make_native(State* vm, Ref dst, const char* name, NativeFn fn);
// Wrap fn in a Function object and define it in the current namespace.
void ot_def_native(State* vm, const char* name, NativeFn fn);

// Register a statically linked module for resolution by `require`. The init
// callback runs once with the current namespace pre-switched to the module's.
typedef void (*NativeModuleInit)(State* vm);
void ot_register_native_module(State* vm, const char* name, NativeModuleInit init);

// --- application and evaluation ---------------------------------------------
//
// Arguments are pushed contiguously (ot_push_copy) starting at an argBase
// taken from ot_top. The result is written to dst; the return Value is nil or
// the unwind sentinel — both immediates — so OT_TRY composes.

Value ot_apply(State* vm, Ref dst, Ref fn, u32 argBase, u32 argc);
Value ot_eval(State* vm, Ref dst, Ref form);

// Read exactly one form from a source string into dst. Returns an unwind on
// read error; otherwise nil, with *outcome 0 on success, 1 for empty input,
// 2 for trailing input after the form.
Value ot_read_string(State* vm, Ref dst, Ref src, u32* outcome);

// --- errors, conditions, restarts -------------------------------------------

// Build {:type 'error :message <formatted>}, signal it, and (if unhandled)
// start a condition unwind. Always returns the unwind sentinel.
Value raise_error(State* vm, const char* fmt, ...);
// Format an interned name into the message at a `%.*s` placeholder.
Value raise_error_sym(State* vm, const char* fmt, u32 symId);

Value ot_signal(State* vm, Ref condition, bool unwindIfUnhandled);
void ot_cancel_unwind(State* vm);
Value ot_start_quit(State* vm);

u32 ot_restart_count(State* vm);
void ot_restart_pop_to(State* vm, u32 count);
u64 ot_add_restart(State* vm, Ref name, Ref description);
void ot_restart_at(State* vm, Ref dst, u32 i);  // 0 = innermost
u32 ot_restart_name(State* vm, Ref r);          // symbol id
void ot_restart_description(State* vm, Ref dst, Ref r);
bool ot_restart_active(State* vm, Ref r);
// Begin a restart unwind targeting `r`, with args at [argBase, argBase+argc).
Value ot_invoke_restart(State* vm, Ref r, u32 argBase, u32 argc);
void ot_make_param(State* vm, Ref dst, u32 name, Ref defaultValue);

// The condition type registry (a table pinned in a well-known root slot).
Ref ot_type_parents(State* vm);

// --- namespaces and vars ----------------------------------------------------

u32 ot_current_ns(State* vm);
void ot_set_current_ns(State* vm, u32 nsName);  // raw switch, no creation
void ot_switch_ns(State* vm, u32 nsName);       // creates if needed
u32 ot_expand_ns(State* vm);                    // 0 = none
void ot_set_expand_ns(State* vm, u32 nsName);
u64 ot_next_gensym(State* vm);

// Var cells are arrays indexed by these slots.
enum OtVarSlot : u32 {
  OT_VAR_VALUE = 0,
  OT_VAR_NAME,
  OT_VAR_NS,
  OT_VAR_DOC,
  OT_VAR_PRIVATE,
  OT_VAR_SLOTS,
};
bool ot_resolve_var(State* vm, Ref dst, Ref sym);  // cell into dst, false on miss
Value ot_resolve(State* vm, Ref dst, Ref sym);     // value into dst; unwinds on miss

// Runtime namespace machinery. Most callers only need ot_resolve/ot_switch_ns;
// these operations are exposed so eval.c and vm.c never need collection
// layouts or raw heap values.
bool ot_ns_lookup(State* vm, Ref dst, u32 nsName);
void ot_ns_get_or_create(State* vm, Ref dst, u32 nsName);
void ot_ns_field(State* vm, Ref dst, Ref nsRecord, u32 kwId);
void ot_define(State* vm, Ref dst, u32 name, Ref value, bool isPrivate, Ref docstring);
void ot_var_value(State* vm, Ref dst, Ref var);
void ot_var_set(State* vm, Ref var, Ref value);
bool ot_var_private(State* vm, Ref var);

// Runtime seams used by eval.c without exposing State internals.
Value ot_require_load(State* vm, u32 nsName);
Value ot_execute_code(State* vm, Ref dst, Ref code);

// --- pre-interned names -----------------------------------------------------
//
// Symbol/keyword ids interned at startup. The X-list generates both this
// struct and its initialization (state.c) so a field cannot be left silently
// uninitialized. Immediate u32 ids only; safe to read from anywhere.
#define OT_SYM_LIST(X)                                                                             \
  X(quote_, "quote")                                                                               \
  X(if_, "if")                                                                                     \
  X(define_, "define")                                                                             \
  X(def_, "def")                                                                                   \
  X(definePriv_, "define-")                                                                        \
  X(setBang_, "set!")                                                                              \
  X(lambda_, "lambda")                                                                             \
  X(fn_, "fn")                                                                                     \
  X(defmacro_, "defmacro")                                                                         \
  X(begin_, "begin")                                                                               \
  X(do_, "do")                                                                                     \
  X(let_, "let")                                                                                   \
  X(while_, "while")                                                                               \
  X(and_, "and")                                                                                   \
  X(or_, "or")                                                                                     \
  X(cond_, "cond")                                                                                 \
  X(else_, "else")                                                                                 \
  X(quasiquote_, "quasiquote")                                                                     \
  X(unquote_, "unquote")                                                                           \
  X(unquoteSplicing_, "unquote-splicing")                                                          \
  X(ns_, "ns")                                                                                     \
  X(inNs_, "in-ns")                                                                                \
  X(require_, "require")                                                                           \
  X(handlerBind_, "handler-bind")                                                                  \
  X(restartCase_, "restart-case")                                                                  \
  X(try_, "try")                                                                                   \
  X(catch_, "catch")                                                                               \
  X(unwindProtect_, "unwind-protect")                                                              \
  X(defer_, "defer")                                                                               \
  X(defparam_, "defparam")                                                                         \
  X(withParams_, "with-params")                                                                    \
  X(array_, "array")                                                                               \
  X(table_, "table")                                                                               \
  X(amp_, "&")                                                                                     \
  X(otiumCore_, "otium.core")                                                                      \
  X(user_, "user")                                                                                 \
  X(expander_, "*expander*")                                                                       \
  X(error_, "error")                                                                               \
  X(quit_, "quit")                                                                                 \
  X(kwType, "type")                                                                                \
  X(kwMessage, "message")                                                                          \
  X(kwData, "data")                                                                                \
  X(kwName, "name")                                                                                \
  X(kwVars, "vars")                                                                                \
  X(kwAliases, "aliases")                                                                          \
  X(kwRefers, "refers")                                                                            \
  X(kwOrder, "order")                                                                              \
  X(kwAs, "as")                                                                                    \
  X(kwRefer, "refer")                                                                              \
  X(kwReload, "reload")                                                                            \
  X(kwRequire, "require")

typedef struct Syms {
#define OT_SYM_FIELD(field, text) u32 field;
  OT_SYM_LIST(OT_SYM_FIELD)
#undef OT_SYM_FIELD
} Syms;

const Syms* ot_syms(State* vm);
bool ot_sym_qualified(State* vm, u32 symId);

// --- foreign objects (extensions) -------------------------------------------

typedef void (*ForeignFinalizer)(State* vm, void* payload);
u32 ot_register_foreign_type(State* vm, const char* name, ForeignFinalizer finalize);
void ot_make_foreign_inline(State* vm, Ref dst, u32 typeId, const void* payload, u32 payloadBytes);
void ot_make_foreign_pointer(State* vm, Ref dst, u32 typeId, void* payload);
u32 ot_foreign_type_id(State* vm, Ref v);  // 0 when not a foreign
// On success writes the payload address to *out and returns nil. CAUTION: for
// inline payloads that address is into the GC heap — use it before the next
// allocating call, or allocate the payload in external mode.
Value ot_foreign_check(State* vm, const char* who, Ref v, u32 expectedType, void** out);
Value ot_foreign_release(State* vm, const char* who, Ref v, u32 expectedType);
