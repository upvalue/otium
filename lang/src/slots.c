// slots.c — implementation of the slot API in slots.h. This file works on
// heap internals: each function derives raw pointers or Values from rooted
// slots and finishes with them before anything can allocate, or roots
// explicitly across the allocating call. The discipline is concentrated here
// so files using slots.h need none of it.
#define OT_HEAP_INTERNALS
#include "slots.h"
#include "state.h"
#include "heap.h"
#include "collections.h"
#include "eval.h"
#include "printer.h"
#include "code.h"
#include "intern.h"
#include "reader.h"
#include "vm.h"

// --- scopes and slots --------------------------------------------------------

void ot_scope_release(ScopeGuard* g) {
  if (g->vm && g->vm->stack.len > g->base) g->vm->stack.len = g->base;
}

ScopeGuard ot_scope_open(State* vm) { return (ScopeGuard){vm, vm->stack.len}; }

Ref ot_push(State* vm) { return ref_push(vm, nil_v()); }

void ot_pop_to(State* vm, u32 top) {
  OT_ASSERT(top <= vm->stack.len);
  vm->stack.len = top;
}
Ref ot_push_copy(State* vm, Ref src) { return ref_push(vm, ref_get(vm, src)); }
Ref ot_push_im(State* vm, Value immediate) {
  OT_ASSERT(!is_heap(immediate));
  return ref_push(vm, immediate);
}
void ot_copy(State* vm, Ref dst, Ref src) { ref_set(vm, dst, ref_get(vm, src)); }
void ot_set_return(State* vm, Ref dst, Value returned) { ref_set(vm, dst, returned); }
u32 ot_top(State* vm) { return vm->stack.len; }
Value ot_ret(State* vm, Ref r) { return ref_get(vm, r); }

// --- scalars -----------------------------------------------------------------

Tag ot_tag(State* vm, Ref r) { return ref_get(vm, r).tag; }
bool ot_nil(State* vm, Ref r) { return is_nil(ref_get(vm, r)); }
bool ot_truthy(State* vm, Ref r) { return is_truthy(ref_get(vm, r)); }

i64 ot_int(State* vm, Ref r) {
  Value v = ref_get(vm, r);
  OT_ASSERT(v.tag == Tag_Int);
  return v.i;
}

f64 ot_float(State* vm, Ref r) {
  Value v = ref_get(vm, r);
  OT_ASSERT(v.tag == Tag_Float);
  return v.f;
}

f64 ot_num(State* vm, Ref r) {
  Value v = ref_get(vm, r);
  OT_ASSERT(v.tag == Tag_Int || v.tag == Tag_Float);
  return v.tag == Tag_Int ? (f64)v.i : v.f;
}

u32 ot_id(State* vm, Ref r) {
  Value v = ref_get(vm, r);
  OT_ASSERT(v.tag == Tag_Symbol || v.tag == Tag_Keyword);
  return v.id;
}

void ot_set_nil(State* vm, Ref r) { ref_set(vm, r, nil_v()); }
void ot_set_null(State* vm, Ref r) { ref_set(vm, r, null_v()); }
void ot_set_bool(State* vm, Ref r, bool b) { ref_set(vm, r, bool_v(b)); }
void ot_set_int(State* vm, Ref r, i64 i) { ref_set(vm, r, int_v(i)); }
void ot_set_float(State* vm, Ref r, f64 f) { ref_set(vm, r, float_v(f)); }
void ot_set_symbol(State* vm, Ref r, u32 id) { ref_set(vm, r, symbol_v(id)); }
void ot_set_keyword(State* vm, Ref r, u32 id) { ref_set(vm, r, keyword_v(id)); }

static Value need_number_args_raw(State* vm, const char* who, u32 base, u32 argc) {
  for (u32 i = 0; i < argc; i++) {
    Tag tag = vm->stack.data[base + i].tag;
    if (tag != Tag_Int && tag != Tag_Float) return raise_error(vm, "%s: expected number", who);
  }
  return nil_v();
}

static f64 raw_num(Value value) { return value.tag == Tag_Int ? (f64)value.i : value.f; }

Value ot_num_add_args(State* vm, u32 base, u32 argc) {
  OT_TRY(need_number_args_raw(vm, "+", base, argc));
  bool hasFloat = false;
  for (u32 i = 0; i < argc; i++) hasFloat |= vm->stack.data[base + i].tag == Tag_Float;
  if (hasFloat) {
    f64 sum = 0.0;
    for (u32 i = 0; i < argc; i++) sum += raw_num(vm->stack.data[base + i]);
    return float_v(sum);
  }
  u64 sum = 0;
  for (u32 i = 0; i < argc; i++) sum += (u64)vm->stack.data[base + i].i;
  return int_v((i64)sum);
}

Value ot_num_mul_args(State* vm, u32 base, u32 argc) {
  OT_TRY(need_number_args_raw(vm, "*", base, argc));
  bool hasFloat = false;
  for (u32 i = 0; i < argc; i++) hasFloat |= vm->stack.data[base + i].tag == Tag_Float;
  if (hasFloat) {
    f64 product = 1.0;
    for (u32 i = 0; i < argc; i++) product *= raw_num(vm->stack.data[base + i]);
    return float_v(product);
  }
  u64 product = 1;
  for (u32 i = 0; i < argc; i++) product *= (u64)vm->stack.data[base + i].i;
  return int_v((i64)product);
}

Value ot_num_sub_args(State* vm, u32 base, u32 argc) {
  if (argc == 0) return raise_error(vm, "-: wrong number of arguments (0)");
  OT_TRY(need_number_args_raw(vm, "-", base, argc));
  bool hasFloat = false;
  for (u32 i = 0; i < argc; i++) hasFloat |= vm->stack.data[base + i].tag == Tag_Float;
  if (hasFloat) {
    f64 difference = raw_num(vm->stack.data[base]);
    if (argc == 1) return float_v(-difference);
    for (u32 i = 1; i < argc; i++) difference -= raw_num(vm->stack.data[base + i]);
    return float_v(difference);
  }
  u64 difference = (u64)vm->stack.data[base].i;
  if (argc == 1) return int_v((i64)(0 - difference));
  for (u32 i = 1; i < argc; i++) difference -= (u64)vm->stack.data[base + i].i;
  return int_v((i64)difference);
}

static int raw_num_cmp(Value a, Value b) {
  if (a.tag == Tag_Int && b.tag == Tag_Int) return a.i < b.i ? -1 : a.i > b.i ? 1 : 0;
  f64 x = raw_num(a), y = raw_num(b);
  return x < y ? -1 : x > y ? 1 : 0;
}

Value ot_num_compare_args(State* vm, u32 base, u32 argc, const char* who, OtNumCompare op) {
  if (argc < 2) return raise_error(vm, "%s: wrong number of arguments (%u)", who, argc);
  OT_TRY(need_number_args_raw(vm, who, base, argc));
  for (u32 i = 0; i + 1 < argc; i++) {
    Value a = vm->stack.data[base + i];
    Value b = vm->stack.data[base + i + 1];
    if ((a.tag == Tag_Float && a.f != a.f) || (b.tag == Tag_Float && b.f != b.f))
      return bool_v(false);
    int cmp = raw_num_cmp(a, b);
    bool ok = false;
    switch (op) {
      case OtNumCompare_Eq: ok = cmp == 0; break;
      case OtNumCompare_Lt: ok = cmp < 0; break;
      case OtNumCompare_Gt: ok = cmp > 0; break;
      case OtNumCompare_Le: ok = cmp <= 0; break;
      case OtNumCompare_Ge: ok = cmp >= 0; break;
    }
    if (!ok) return bool_v(false);
  }
  return bool_v(true);
}

bool ot_eq(State* vm, Ref a, Ref b) { return val_eq(ref_get(vm, a), ref_get(vm, b)); }
bool ot_equal(State* vm, Ref a, Ref b) { return val_equal(vm, ref_get(vm, a), ref_get(vm, b)); }

// --- constructors ------------------------------------------------------------
// make_* root their heap-valued arguments internally (heap tempRoots), and
// every result lands in its destination slot before anything else can run.

void ot_make_array(State* vm, Ref dst, u32 cap) { ref_set(vm, dst, make_array(vm, cap)); }
void ot_make_table(State* vm, Ref dst) { ref_set(vm, dst, make_table(vm)); }
void ot_make_buffer(State* vm, Ref dst) { ref_set(vm, dst, make_buffer(vm)); }

void ot_make_string(State* vm, Ref dst, const char* bytes, u32 len) {
  ref_set(vm, dst, make_string(vm, bytes, len));
}

void ot_make_string_buf(State* vm, Ref dst, const Buf* b) {
  ref_set(vm, dst, make_string_buf(vm, b));
}

void ot_cons(State* vm, Ref dst, Ref car, Ref cdr) {
  ref_set(vm, dst, make_pair(vm, ref_get(vm, car), ref_get(vm, cdr)));
}

// --- pairs -------------------------------------------------------------------

void ot_car(State* vm, Ref dst, Ref pair) { ref_set(vm, dst, as_pair(ref_get(vm, pair))->car); }
void ot_cdr(State* vm, Ref dst, Ref pair) { ref_set(vm, dst, as_pair(ref_get(vm, pair))->cdr); }

void ot_pair_set(State* vm, Ref pair, bool car, Ref v) {
  if (car) as_pair(ref_get(vm, pair))->car = ref_get(vm, v);
  else as_pair(ref_get(vm, pair))->cdr = ref_get(vm, v);
}

bool ot_pair_key_frozen(State* vm, Ref pair) { return pair_key_frozen(ref_get(vm, pair)); }

bool ot_pair_contains(State* vm, Ref root, Ref needle) {
  return pair_contains(ref_get(vm, root), ref_get(vm, needle));
}

void ot_list_from_stack(State* vm, Ref dst, u32 base, u32 n, Ref tail) {
  ref_set(vm, dst, list_from_stack_onto(vm, base, n, ref_get(vm, tail)));
}

// --- arrays ------------------------------------------------------------------

u32 ot_array_len(State* vm, Ref arr) { return as_array(ref_get(vm, arr))->len; }

void ot_array_get(State* vm, Ref dst, Ref arr, i64 i) {
  ref_set(vm, dst, array_get(ref_get(vm, arr), i));
}

bool ot_array_set(State* vm, Ref arr, i64 i, Ref v) {
  ArrayData* a = as_array(ref_get(vm, arr));
  if (i < 0 || (u64)i >= a->len) return false;
  // No allocation between the derive and the write.
  array_items(ref_get(vm, arr))[i] = ref_get(vm, v);
  return true;
}

void ot_array_push(State* vm, Ref arr, Ref v) { array_push(vm, arr, v); }
void ot_array_push_im(State* vm, Ref arr, Value imm) { array_push_im(vm, arr, imm); }

void ot_array_pop(State* vm, Ref dst, Ref arr) {
  ArrayData* a = as_array(ref_get(vm, arr));
  if (a->len == 0) {
    ref_set(vm, dst, nil_v());
    return;
  }
  Value popped = array_items(ref_get(vm, arr))[--a->len];
  // Clear the vacated slot: storage traces its whole capacity, so leaving the
  // value there would keep it alive after the pop.
  array_items(ref_get(vm, arr))[a->len] = nil_v();
  ref_set(vm, dst, popped);
}

// --- tables ------------------------------------------------------------------

u32 ot_table_count(State* vm, Ref t) { return table_entry_count(ref_get(vm, t)); }

void ot_table_get(State* vm, Ref dst, Ref t, Ref key) {
  ref_set(vm, dst, table_get(vm, ref_get(vm, t), ref_get(vm, key)));
}

void ot_table_get_im(State* vm, Ref dst, Ref t, Value immKey) {
  if (is_heap(immKey)) ot_fatal("ot_table_get_im: key must be an immediate");
  ref_set(vm, dst, table_get(vm, ref_get(vm, t), immKey));
}

void ot_table_put(State* vm, Ref t, Ref key, Ref v) { table_put(vm, t, key, v); }
void ot_table_put_im(State* vm, Ref t, Value immKey, Ref v) { table_put_iv(vm, t, immKey, v); }
void ot_table_put_im2(State* vm, Ref t, Value immKey, Value immVal) {
  table_put_ii(vm, t, immKey, immVal);
}

bool ot_table_next(State* vm, Ref t, u32* cursor, Ref k, Ref v) {
  Value key, val;
  if (!table_iter_next(ref_get(vm, t), cursor, &key, &val)) return false;
  ref_set(vm, k, key);
  ref_set(vm, v, val);
  return true;
}

Value ot_collection_get(State* vm, Ref dst, Ref collection, Ref key, Ref dflt) {
  Value coll = ref_get(vm, collection);
  Value k = ref_get(vm, key);
  Value result = nil_v();
  switch (coll.tag) {
    case Tag_Nil: break;
    case Tag_Table: result = table_get(vm, coll, k); break;
    case Tag_Array:
      if (k.tag == Tag_Int) result = array_get(coll, k.i);
      break;
    case Tag_String:
      if (k.tag == Tag_Int && k.i >= 0 && (u64)k.i < as_string(coll)->nchars) {
        u32 start = ot_string_utf8_offset(vm, collection, (u32)k.i);
        u32 end = ot_string_utf8_offset(vm, collection, (u32)k.i + 1);
        ot_substring(vm, dst, collection, start, end - start);
        return nil_v();
      }
      break;
    default: return raise_error(vm, "get: unsupported collection type");
  }
  ref_set(vm, dst, is_nil(result) ? ref_get(vm, dflt) : result);
  return nil_v();
}

Value ot_collection_put(State* vm, Ref collection, Ref key, Ref value) {
  Value coll = ref_get(vm, collection);
  if (coll.tag == Tag_Table) {
    table_put(vm, collection, key, value);
    return nil_v();
  }
  if (coll.tag == Tag_Array) {
    Value k = ref_get(vm, key);
    if (k.tag != Tag_Int) return raise_error(vm, "put!: array index must be an int");
    ArrayData* array = as_array(coll);
    if (k.i < 0 || (u64)k.i >= array->len)
      return raise_error(vm, "put!: array index out of range");
    // No allocation between deriving the storage and writing the element.
    array_items(coll)[k.i] = ref_get(vm, value);
    return nil_v();
  }
  return raise_error(vm, "put!: expected table or array");
}

Value ot_array_push_args(State* vm, Ref array, u32 base, u32 count) {
  if (ot_tag(vm, array) != Tag_Array) return raise_error(vm, "push!: expected array");
  for (u32 i = 0; i < count; i++) array_push(vm, array, (Ref){base + i});
  return nil_v();
}

// --- strings -----------------------------------------------------------------
// All reads below are allocation-free: pointers derived from the rooted slot
// are dead before anything can move the string.

u32 ot_string_len(State* vm, Ref s) { return as_string(ref_get(vm, s))->len; }
u32 ot_string_nchars(State* vm, Ref s) { return as_string(ref_get(vm, s))->nchars; }

u8 ot_string_byte(State* vm, Ref s, u32 i) {
  StringData* d = as_string(ref_get(vm, s));
  OT_ASSERT(i < d->len);
  return (u8)string_data_bytes(d)[i];
}

u32 ot_string_utf8_offset(State* vm, Ref s, u32 nth) {
  StringData* d = as_string(ref_get(vm, s));
  const char* p = string_data_bytes(d);
  u32 i = 0, c = 0;
  while (i < d->len && c < nth) {
    i++;
    while (i < d->len && ((u8)p[i] & 0xC0) == 0x80) i++;
    c++;
  }
  return i;
}

void ot_substring(State* vm, Ref dst, Ref s, u32 byteOff, u32 byteLen) {
  ref_set(vm, dst, make_string_from(vm, ref_get(vm, s), byteOff, byteLen));
}

void ot_string_copy(State* vm, Ref s, u32 byteOff, u32 byteLen, Buf* out) {
  StringData* d = as_string(ref_get(vm, s));
  OT_ASSERT(byteOff <= d->len && byteLen <= d->len - byteOff);
  buf_append(out, string_data_bytes(d) + byteOff, byteLen);
}

int ot_string_cmp(State* vm, Ref a, Ref b) {
  StringData* sa = as_string(ref_get(vm, a));
  StringData* sb = as_string(ref_get(vm, b));
  u32 n = sa->len < sb->len ? sa->len : sb->len;
  int c = memcmp(string_data_bytes(sa), string_data_bytes(sb), n);
  if (c) return c;
  return sa->len < sb->len ? -1 : sa->len > sb->len ? 1 : 0;
}

bool ot_string_region_eq(State* vm, Ref a, u32 aOff, Ref b, u32 bOff, u32 len) {
  StringData* sa = as_string(ref_get(vm, a));
  StringData* sb = as_string(ref_get(vm, b));
  if (aOff > sa->len || len > sa->len - aOff) return false;
  if (bOff > sb->len || len > sb->len - bOff) return false;
  return memcmp(string_data_bytes(sa) + aOff, string_data_bytes(sb) + bOff, len) == 0;
}

bool ot_string_find(State* vm, Ref hay, Ref needle, u32 fromByte, u32* atByte) {
  StringData* h = as_string(ref_get(vm, hay));
  StringData* n = as_string(ref_get(vm, needle));
  const char* hp = string_data_bytes(h);
  const char* np = string_data_bytes(n);
  if (n->len > h->len || fromByte > h->len - n->len) return false;
  for (u32 i = fromByte; i + n->len <= h->len; i++)
    if (memcmp(hp + i, np, n->len) == 0) {
      if (atByte) *atByte = i;
      return true;
    }
  return false;
}

// --- buffers -----------------------------------------------------------------

u32 ot_buffer_len(State* vm, Ref b) { return as_buffer(ref_get(vm, b))->len; }

u32 ot_buffer_nchars(State* vm, Ref b) {
  Value v = ref_get(vm, b);
  return utf8_count(buffer_data(v), buffer_len(v));
}

void ot_buffer_copy(State* vm, Ref b, Buf* out) {
  Value value = ref_get(vm, b);
  buf_append(out, buffer_data(value), buffer_len(value));
}

void ot_buffer_append(State* vm, Ref b, const char* src, u32 n) { buffer_append(vm, b, src, n); }

void ot_buffer_to_string(State* vm, Ref dst, Ref b) {
  ref_set(vm, dst, make_string_from_buffer(vm, ref_get(vm, b)));
}

// --- interning and names -----------------------------------------------------

u32 ot_intern(State* vm, const char* s, u32 len) { return intern_id(&vm->intern, s, len); }

const char* ot_intern_name(State* vm, u32 id, u32* lenOut) {
  return intern_name(&vm->intern, id, lenOut);
}

u32 ot_name_id(State* vm, Ref v) {
  Value val = ref_get(vm, v);
  if (val.tag == Tag_Symbol || val.tag == Tag_Keyword) return val.id;
  if (val.tag == Tag_String) {
    StringData* s = as_string(val);
    // intern_id copies into C-heap storage and cannot collect.
    return intern_id(&vm->intern, string_data_bytes(s), s->len);
  }
  return 0;
}

// --- printing and output -----------------------------------------------------
void ot_display(State* vm, Ref v, Buf* out) { print_ref_display(vm, v, out); }
void ot_repr(State* vm, Ref v, Buf* out) { print_ref_repr(vm, v, out); }

u32 ot_callable_name(State* vm, Ref fn) { return as_function(ref_get(vm, fn))->name; }

bool ot_callable_code(State* vm, Ref dst, Ref fn) {
  Value code = as_function(ref_get(vm, fn))->code;
  ref_set(vm, dst, code);
  return code.tag == Tag_Code;
}

u32 ot_param_name(State* vm, Ref param) { return as_param(ref_get(vm, param))->name; }

u32 ot_foreign_name(State* vm, Ref foreign) {
  const ForeignType* type = heap_foreign_type(&vm->heap, as_foreign(ref_get(vm, foreign))->typeId);
  return type ? type->nameSym : 0;
}

void ot_code_ascii(State* vm, Ref code, Buf* out) { code_print_ascii_ref(vm, code, out); }

// --- code objects ------------------------------------------------------------

Value make_code_ref(State* vm, Ref dst, const u8* bytes, u32 len, Ref constants,
                    const CodeSpec* spec) {
  if (ot_tag(vm, constants) != Tag_Array) return raise_error(vm, "code constants must be an array");
  u32 constCount = ot_array_len(vm, constants);
  if (constCount > (UINT32_MAX - (u32)sizeof(CodeData) - len) / (u32)sizeof(Value))
    ot_fatal("code: size overflow");
  u32 size = (u32)sizeof(CodeData) + constCount * (u32)sizeof(Value) + len;
  Obj* obj = heap_alloc(&vm->heap, ObjType_Code, size);
  CodeData* code = (CodeData*)obj_payload(obj);
  code->len = len;
  code->constCount = constCount;
  code->nfixed = spec->nfixed;
  code->hasRest = spec->hasRest;
  code->nupvals = spec->nupvals;
  code->nlocals = spec->nlocals;
  code->maxStack = spec->maxStack;
  code->name = spec->name;
  if (constCount)
    memcpy(code_consts(code), array_items(ref_get(vm, constants)),
           (size_t)constCount * sizeof(Value));
  if (len) memcpy(code_bytes(code), bytes, len);
  ref_set(vm, dst, obj_v(Tag_Code, obj));
  return nil_v();
}

u32 code_len_ref(State* vm, Ref code) { return as_code(ref_get(vm, code))->len; }
u32 code_const_count_ref(State* vm, Ref code) { return as_code(ref_get(vm, code))->constCount; }
u32 code_name_ref(State* vm, Ref code) { return as_code(ref_get(vm, code))->name; }
u8 code_byte_ref(State* vm, Ref code, u32 at) {
  CodeData* data = as_code(ref_get(vm, code));
  OT_ASSERT(at < data->len);
  return code_bytes(data)[at];
}
void code_constant_ref(State* vm, Ref dst, Ref code, u32 index) {
  CodeData* data = as_code(ref_get(vm, code));
  OT_ASSERT(index < data->constCount);
  ref_set(vm, dst, code_consts(data)[index]);
}

void ot_write_out(State* vm, const char* s, u32 n) {
  if (vm->writeFn && n) vm->writeFn(vm->writeUd, s, n);
}

// --- sequence iteration ------------------------------------------------------

void seq_iter_init(SeqIter* it, State* vm, Ref rootedSequence) {
  it->vm = vm;
  it->cursor = rootedSequence;
  it->index = 0;
  it->limit = 0;
  it->kind = SeqKind_Invalid;
  Value seq = ref_get(vm, it->cursor);
  if (seq.tag == Tag_Array) {
    it->kind = SeqKind_Array;
    it->limit = as_array(seq)->len;
  } else if (seq.tag == Tag_Pair || seq.tag == Tag_Null) {
    it->kind = SeqKind_List;
  } else if (is_nil(seq)) {
    it->kind = SeqKind_Empty;
  }
}

SeqStep seq_iter_next(SeqIter* it, Ref item) {
  switch (it->kind) {
    case SeqKind_Empty: return SeqStep_End;
    case SeqKind_Invalid: return SeqStep_NotSequence;
    case SeqKind_Array: {
      if (it->index >= it->limit) return SeqStep_End;
      // Re-derived from the rooted cursor on every step: a callback between
      // steps can allocate and move the backing store.
      Value seq = ref_get(it->vm, it->cursor);
      // for-each historically snapshots the length and uses get for each
      // index, so shrinking during a callback yields nil for removed slots.
      ref_set(it->vm, item,
              it->index < as_array(seq)->len ? array_items(seq)[it->index] : nil_v());
      it->index++;
      return SeqStep_Item;
    }
    case SeqKind_List: {
      Value cursor = ref_get(it->vm, it->cursor);
      if (cursor.tag == Tag_Null) return SeqStep_End;
      if (cursor.tag != Tag_Pair) return SeqStep_Improper;
      PairData* pair = as_pair(cursor);
      ref_set(it->vm, item, pair->car);
      ref_set(it->vm, it->cursor, pair->cdr);
      return SeqStep_Item;
    }
  }
  return SeqStep_NotSequence;
}

Value sequence_error(State* vm, const char* who, SeqStep step) {
  if (step == SeqStep_Improper) return raise_error(vm, "%s: improper list", who);
  return raise_error(vm, "%s: expected sequence", who);
}

// --- natives, application, evaluation ----------------------------------------

static Value param_read(State* vm, Value param) {
  for (u32 i = vm->paramBindings.len; i-- > 0;)
    if (vm->paramBindings.data[i].param.obj == param.obj)
      return vm->paramBindings.data[i].value;
  return as_param(param)->defaultVal;
}

Value apply(State* vm, Value callee, u32 base, u32 argc) {
  switch (callee.tag) {
    case Tag_Macro:
    case Tag_Function: {
      FunctionData* function = as_function(callee);
      if (function->native) return function->native(vm, base, argc);
      if (function->code.tag == Tag_Code) return vm_call(vm, callee, base, argc);
      return raise_error(vm, "function has no implementation");
    }
    case Tag_Table: {
      if (argc < 1 || argc > 2) return raise_error(vm, "table call: 1 or 2 arguments");
      Value value = table_get(vm, callee, vm->stack.data[base]);
      if (is_nil(value) && argc == 2) value = vm->stack.data[base + 1];
      return value;
    }
    case Tag_Array: {
      if (argc < 1 || argc > 2) return raise_error(vm, "array call: 1 or 2 arguments");
      Value key = vm->stack.data[base];
      Value value = key.tag == Tag_Int ? array_get(callee, key.i) : nil_v();
      if (is_nil(value) && argc == 2) value = vm->stack.data[base + 1];
      return value;
    }
    case Tag_Keyword: {
      if (argc < 1 || argc > 2) return raise_error(vm, "keyword call: 1 or 2 arguments");
      Value collection = vm->stack.data[base];
      Value value = collection.tag == Tag_Table ? table_get(vm, collection, callee) : nil_v();
      if (is_nil(value) && argc == 2) value = vm->stack.data[base + 1];
      return value;
    }
    case Tag_Param:
      if (argc != 0) return raise_error(vm, "params take no arguments");
      return param_read(vm, callee);
    default: return raise_error(vm, "value is not callable");
  }
}

static Value control_apply0(State* vm, Value fn) { return apply(vm, fn, vm->stack.len, 0); }

static Value control_apply1(State* vm, Value fn, Value arg) {
  OT_SCOPE(vm);
  Ref fnRoot = ref_push(vm, fn);
  Ref argRoot = ref_push(vm, arg);
  return apply(vm, ref_get(vm, fnRoot), argRoot.i, 1);
}

Value vm_control_handler_bind(State* vm, u32 base, u32 argc) {
  if (argc == 0 || (argc - 1) % 2 != 0) return raise_error(vm, "handler-bind: bad compiled form");
  u32 handlerBase = vm->handlers.len;
  for (u32 i = 0; i + 1 < argc; i += 2)
    vec_push(&vm->handlers,
             ((HandlerBinding){vm->stack.data[base + i], vm->stack.data[base + i + 1]}));
  Value result = control_apply0(vm, vm->stack.data[base + argc - 1]);
  vm->handlers.len = handlerBase;
  return result;
}

Value vm_control_restart_case(State* vm, u32 base, u32 argc) {
  if (argc == 0 || (argc - 1) % 3 != 0) return raise_error(vm, "restart-case: bad compiled form");
  u32 restartBase = vm->restarts.len;
  u64 firstId = 0;
  u32 count = (argc - 1) / 3;
  for (u32 i = 0; i < count; i++) {
    u32 arg = base + 1 + i * 3;
    if (vm->stack.data[arg].tag != Tag_Symbol) {
      ot_restart_pop_to(vm, restartBase);
      return raise_error(vm, "restart-case: bad name");
    }
    u64 id = ot_add_restart(vm, (Ref){arg}, (Ref){arg + 1});
    if (i == 0) firstId = id;
  }
  Value result = control_apply0(vm, vm->stack.data[base]);
  ot_restart_pop_to(vm, restartBase);
  if (result.tag != Tag_Unwind || vm->unwindKind != UnwindKind_Restart ||
      vm->unwindRestartId < firstId || vm->unwindRestartId >= firstId + count)
    return result;
  u32 selected = (u32)(vm->unwindRestartId - firstId);
  OT_SCOPE(vm);
  Ref args = ref_push(vm, vm->unwindRestartArgs);
  vm->unwindKind = UnwindKind_None;
  vm->unwindCondition = nil_v();
  vm->unwindRestartArgs = nil_v();
  Ref cursor = ot_push_copy(vm, args);
  Ref item = ot_push(vm);
  u32 argBase = vm->stack.len;
  u32 argCount = 0;
  while (ot_tag(vm, cursor) == Tag_Pair) {
    ot_car(vm, item, cursor);
    ot_push_copy(vm, item);
    ot_cdr(vm, cursor, cursor);
    argCount++;
  }
  return apply(vm, vm->stack.data[base + 1 + selected * 3 + 2], argBase, argCount);
}

Value vm_control_try(State* vm, u32 base, u32 argc) {
  if (argc == 0 || vm->stack.data[base].tag != Tag_Int || vm->stack.data[base].i < 0)
    return raise_error(vm, "try: bad compiled form");
  u64 bodyCount64 = (u64)vm->stack.data[base].i;
  if (bodyCount64 > argc - 1 || (argc - 1 - bodyCount64) % 2 != 0)
    return raise_error(vm, "try: bad compiled form");
  u32 bodyCount = (u32)bodyCount64;
  Value result = nil_v();
  for (u32 i = 0; i < bodyCount; i++) {
    result = control_apply0(vm, vm->stack.data[base + 1 + i]);
    if (result.tag == Tag_Unwind) break;
  }
  if (result.tag != Tag_Unwind || vm->unwindKind != UnwindKind_Condition) return result;
  OT_SCOPE(vm);
  Ref condition = ref_push(vm, vm->unwindCondition);
  Ref predicate = ref_push(vm, nil_v());
  vm->unwindKind = UnwindKind_None;
  u32 catches = base + 1 + bodyCount;
  for (u32 arg = catches; arg < base + argc; arg += 2) {
    ref_set(vm, predicate, control_apply0(vm, vm->stack.data[arg]));
    if (ref_get(vm, predicate).tag == Tag_Unwind) return unwind_v();
    Value matches = control_apply1(vm, ref_get(vm, predicate), ref_get(vm, condition));
    if (matches.tag == Tag_Unwind) return matches;
    if (is_truthy(matches))
      return control_apply1(vm, vm->stack.data[arg + 1], ref_get(vm, condition));
  }
  vm->unwindKind = UnwindKind_Condition;
  vm->unwindCondition = ref_get(vm, condition);
  return unwind_v();
}

Value vm_control_unwind_protect(State* vm, u32 base, u32 argc) {
  if (argc == 0) return raise_error(vm, "unwind-protect: bad compiled form");
  Value result = control_apply0(vm, vm->stack.data[base]);
  UnwindKind kind = vm->unwindKind;
  OT_SCOPE(vm);
  Ref condition = ref_push(vm, vm->unwindCondition);
  Ref restartArgs = ref_push(vm, vm->unwindRestartArgs);
  u64 restartId = vm->unwindRestartId;
  if (result.tag == Tag_Unwind) vm->unwindKind = UnwindKind_None;
  for (u32 i = 1; i < argc; i++) {
    Value cleanup = control_apply0(vm, vm->stack.data[base + i]);
    if (cleanup.tag == Tag_Unwind) {
      result = cleanup;
      kind = vm->unwindKind;
      ref_set(vm, condition, vm->unwindCondition);
      ref_set(vm, restartArgs, vm->unwindRestartArgs);
      restartId = vm->unwindRestartId;
      vm->unwindKind = UnwindKind_None;
    }
  }
  if (result.tag == Tag_Unwind) {
    vm->unwindKind = kind;
    vm->unwindCondition = ref_get(vm, condition);
    vm->unwindRestartArgs = ref_get(vm, restartArgs);
    vm->unwindRestartId = restartId;
  }
  return result;
}

Value vm_control_with_params(State* vm, u32 base, u32 argc) {
  if (argc == 0 || (argc - 1) % 2 != 0) return raise_error(vm, "with-params: bad compiled form");
  u32 paramBase = vm->paramBindings.len;
  OT_SCOPE(vm);
  for (u32 i = 0; i + 1 < argc; i += 2) {
    Ref paramRoot = ref_push(vm, control_apply0(vm, vm->stack.data[base + i]));
    if (ref_get(vm, paramRoot).tag == Tag_Unwind) {
      vm->paramBindings.len = paramBase;
      return unwind_v();
    }
    if (ref_get(vm, paramRoot).tag != Tag_Param) {
      vm->paramBindings.len = paramBase;
      return raise_error(vm, "with-params: not a param");
    }
    Ref valueRoot = ref_push(vm, control_apply0(vm, vm->stack.data[base + i + 1]));
    if (ref_get(vm, valueRoot).tag == Tag_Unwind) {
      vm->paramBindings.len = paramBase;
      return unwind_v();
    }
    vec_push(&vm->paramBindings,
             ((ParamBinding){ref_get(vm, paramRoot), ref_get(vm, valueRoot)}));
  }
  Value result = control_apply0(vm, vm->stack.data[base + argc - 1]);
  vm->paramBindings.len = paramBase;
  return result;
}

void ot_register_native_module(State* vm, const char* name, NativeModuleInit init) {
  register_native_module(vm, name, init);
}

void ot_def_native(State* vm, const char* name, NativeFn fn) {
  OT_SCOPE(vm);
  Ref native = ot_push(vm);
  ot_make_native(vm, native, name, fn);
  Ref doc = ot_push(vm);
  ot_define(vm, native, intern_id(&vm->intern, name, (u32)strlen(name)), native, false, doc);
}

void ot_make_native(State* vm, Ref dst, const char* name, NativeFn fn) {
  Obj* object = heap_alloc(&vm->heap, ObjType_Function, sizeof(FunctionData));
  FunctionData* data = (FunctionData*)obj_payload(object);
  data->name = intern_id(&vm->intern, name, (u32)strlen(name));
  data->code = nil_v();
  data->nsName = symbol_v(vm->syms.otiumCore_);
  data->native = fn;
  data->docstring = nil_v();
  data->nupvals = 0;
  ref_set(vm, dst, obj_v(Tag_Function, object));
}

Value ot_apply(State* vm, Ref dst, Ref fn, u32 argBase, u32 argc) {
  OT_ASSERT(dst.i < argBase);  // apply pops to argBase; dst must survive it
  Value r = apply(vm, ref_get(vm, fn), argBase, argc);
  if (r.tag == Tag_Unwind) return r;
  ref_set(vm, dst, r);
  return nil_v();
}

Value ot_eval(State* vm, Ref dst, Ref form) {
  return eval_form_ref(vm, dst, form);
}

Value ot_execute_code(State* vm, Ref dst, Ref code) {
  Value result = vm_execute_code(vm, ref_get(vm, code));
  if (result.tag == Tag_Unwind) return result;
  ref_set(vm, dst, result);
  return nil_v();
}

Value ot_read_string(State* vm, Ref dst, Ref src, u32* outcome) {
  // Snapshot the source into a C-heap Buf: the Reader keeps a raw pointer to
  // the source for its whole (allocating) lifetime, so it must never point
  // into the GC heap.
  StringData* s = as_string(ref_get(vm, src));
  u32 srcNchars = s->nchars;
  Buf text = {0};
  buf_append(&text, string_data_bytes(s), s->len);
  Reader r;
  reader_init(&r, vm, text.data ? text.data : "", text.len, "<read-string>");
  Value status = reader_next_ref(&r, dst);
  if (status.tag == Tag_Unwind) {
    buf_deinit(&text);
    return status;
  }
  // Reader returns nil for both EOF and a nil literal, so only a zero-byte
  // source can be identified as empty here.
  if (reader_at_eof(&r) && ot_nil(vm, dst) && srcNchars == 0) {
    buf_deinit(&text);
    *outcome = 1;
    return nil_v();
  }
  Ref trailing = ot_push(vm);
  status = reader_next_ref(&r, trailing);
  if (status.tag == Tag_Unwind) {
    buf_deinit(&text);
    return status;
  }
  *outcome = reader_at_eof(&r) ? 0 : 2;
  buf_deinit(&text);
  return nil_v();
}

// --- errors, conditions, restarts --------------------------------------------

Value ot_signal(State* vm, Ref condition, bool unwindIfUnhandled) {
  return signal_value(vm, ref_get(vm, condition), unwindIfUnhandled);
}

void ot_cancel_unwind(State* vm) { state_cancel_unwind(vm); }

Value ot_start_quit(State* vm) {
  OT_SCOPE(vm);
  Ref condition = ot_push(vm);
  ot_make_table(vm, condition);
  ot_table_put_im2(vm, condition, keyword_v(vm->syms.kwType), symbol_v(vm->syms.quit_));
  vm->unwindCondition = ref_get(vm, condition);
  vm->unwindKind = UnwindKind_Quit;
  return unwind_v();
}

u32 ot_restart_count(State* vm) { return vm->restarts.len; }

void ot_restart_pop_to(State* vm, u32 count) {
  OT_ASSERT(count <= vm->restarts.len);
  vm->restarts.len = count;
}

u64 ot_add_restart(State* vm, Ref name, Ref description) {
  OT_ASSERT(ot_tag(vm, name) == Tag_Symbol);
  Obj* object = heap_alloc(&vm->heap, ObjType_Restart, sizeof(RestartData));
  RestartData* data = (RestartData*)obj_payload(object);
  data->name = ot_id(vm, name);
  data->description = ref_get(vm, description);
  data->restartId = ++vm->restartIdCounter;
  vec_push(&vm->restarts, ((RestartRec){obj_v(Tag_Restart, object)}));
  return data->restartId;
}

void ot_restart_at(State* vm, Ref dst, u32 i) {
  OT_ASSERT(i < vm->restarts.len);
  ref_set(vm, dst, vm->restarts.data[vm->restarts.len - 1 - i].restart);  // 0 = innermost
}

u32 ot_restart_name(State* vm, Ref r) { return as_restart(ref_get(vm, r))->name; }

void ot_restart_description(State* vm, Ref dst, Ref r) {
  ref_set(vm, dst, as_restart(ref_get(vm, r))->description);
}

bool ot_restart_active(State* vm, Ref r) {
  Value target = ref_get(vm, r);
  for (u32 i = vm->restarts.len; i-- > 0;)
    if (vm->restarts.data[i].restart.obj == target.obj) return true;
  return false;
}

Value ot_invoke_restart(State* vm, Ref r, u32 argBase, u32 argc) {
  // Read the id before list_from_stack allocates: the restart object moves.
  u64 rid = as_restart(ref_get(vm, r))->restartId;
  vm->unwindRestartArgs = list_from_stack(vm, argBase, argc);
  vm->unwindRestartId = rid;
  vm->unwindCondition = nil_v();
  vm->unwindKind = UnwindKind_Restart;
  return unwind_v();
}

void ot_make_param(State* vm, Ref dst, u32 name, Ref defaultValue) {
  Obj* object = heap_alloc(&vm->heap, ObjType_Param, sizeof(ParamData));
  ParamData* data = (ParamData*)obj_payload(object);
  data->name = name;
  data->defaultVal = ref_get(vm, defaultValue);
  ref_set(vm, dst, obj_v(Tag_Param, object));
}

Ref ot_type_parents(State* vm) {
  // state_create pins the condition type registry in stack[1].
  (void)vm;
  return (Ref){1};
}

// --- namespaces and vars -----------------------------------------------------

u32 ot_current_ns(State* vm) { return vm->currentNs; }
void ot_set_current_ns(State* vm, u32 nsName) { vm->currentNs = nsName; }
u32 ot_expand_ns(State* vm) { return vm->expandNs; }
void ot_set_expand_ns(State* vm, u32 nsName) { vm->expandNs = nsName; }
u64 ot_next_gensym(State* vm) { return ++vm->gensymCounter; }

const Syms* ot_syms(State* vm) { return &vm->syms; }

Value ot_require_load(State* vm, u32 nsName) {
  u32 len = 0;
  const char* name = intern_name(&vm->intern, nsName, &len);
  NativeModule* native = find_native_module(vm, nsName);
  {
    OT_SCOPE(vm);
    Ref existing = ot_push(vm);
    bool loaded = ot_ns_lookup(vm, existing, nsName);
    if ((!native || native->initialized) && loaded) return nil_v();
  }
  for (u32 i = 0; i < vm->loadingNs.len; i++)
    if (vm->loadingNs.data[i] == nsName)
      return raise_error(vm, "circular require: %.*s", (int)len, name);

  char cname[256];
  snprintf(cname, sizeof cname, "%.*s", (int)len, name);
  vec_push(&vm->loadingNs, nsName);
  u32 savedNs = vm->currentNs;
  bool nativeHit = native != nullptr;
  if (native && !native->initialized) {
    NativeModuleInit init = native->init;
    native->initialized = true;
    ot_switch_ns(vm, nsName);
    init(vm);
  }

  Buf source = {0};
  bool sourceHit = vm->loadFn && vm->loadFn(vm->loadUd, cname, &source);
  Value result = nil_v();
  if (sourceHit) {
    ot_switch_ns(vm, nsName);
    result = eval_source(vm, source.data, source.len, cname);
  } else if (!nativeHit) {
    result = vm->loadFn ? raise_error(vm, "namespace not found on load path: %s", cname)
                        : raise_error(vm, "namespace not found: %.*s", (int)len, name);
  }
  buf_deinit(&source);
  vm->currentNs = savedNs;
  vec_pop(&vm->loadingNs);
  return result.tag == Tag_Unwind ? result : nil_v();
}

// --- foreign objects ---------------------------------------------------------

static u32 foreign_payload_bytes(u32 payloadBytes) {
  if (payloadBytes > UINT32_MAX - (u32)sizeof(ForeignData))
    ot_fatal("foreign: payload size overflow");
  return (u32)sizeof(ForeignData) + payloadBytes;
}

u32 ot_register_foreign_type(State* vm, const char* name, ForeignFinalizer finalize) {
  u32 nameSym = intern_id(&vm->intern, name, (u32)strlen(name));
  return heap_add_foreign_type(&vm->heap, nameSym, finalize);
}

void ot_make_foreign_inline(State* vm, Ref dst, u32 typeId, const void* payload,
                            u32 payloadBytes) {
  if (!heap_foreign_type(&vm->heap, typeId)) ot_fatal("foreign: invalid type id");
  if (payloadBytes && !payload) ot_fatal("foreign: null inline payload");
  Obj* object = heap_alloc(&vm->heap, ObjType_Foreign, foreign_payload_bytes(payloadBytes));
  ForeignData* data = (ForeignData*)obj_payload(object);
  data->typeId = typeId;
  data->flags = 0;
  data->payloadSize = payloadBytes;
  if (payloadBytes) memcpy(data + 1, payload, payloadBytes);
  ref_set(vm, dst, obj_v(Tag_Foreign, object));
}

void ot_make_foreign_pointer(State* vm, Ref dst, u32 typeId, void* payload) {
  if (!heap_foreign_type(&vm->heap, typeId)) ot_fatal("foreign: invalid type id");
  Obj* object = heap_alloc(&vm->heap, ObjType_Foreign, foreign_payload_bytes(sizeof payload));
  ForeignData* data = (ForeignData*)obj_payload(object);
  data->typeId = typeId;
  data->flags = ForeignExternal;
  data->payloadSize = sizeof payload;
  memcpy(data + 1, &payload, sizeof payload);
  ref_set(vm, dst, obj_v(Tag_Foreign, object));
}

u32 ot_foreign_type_id(State* vm, Ref v) {
  Value val = ref_get(vm, v);
  return val.tag == Tag_Foreign ? as_foreign(val)->typeId : 0;
}

Value ot_foreign_check(State* vm, const char* who, Ref v, u32 expectedType, void** out) {
  const ForeignType* type = heap_foreign_type(&vm->heap, expectedType);
  u32 len = 7;
  const char* name = "foreign";
  if (type) {
    const char* interned = intern_name(&vm->intern, type->nameSym, &len);
    if (interned) name = interned;
  }
  Value value = ref_get(vm, v);
  if (value.tag != Tag_Foreign)
    return raise_error(vm, "%s: expected %.*s", who, (int)len, name);
  ForeignData* data = as_foreign(value);
  if (data->typeId != expectedType)
    return raise_error(vm, "%s: expected %.*s", who, (int)len, name);
  if (data->flags & ForeignDead)
    return raise_error(vm, "%s: %.*s has been released", who, (int)len, name);
  if (data->flags & ForeignExternal) memcpy(out, data + 1, sizeof *out);
  else *out = data + 1;
  return nil_v();
}

Value ot_foreign_release(State* vm, const char* who, Ref v, u32 expectedType) {
  void* payload = nullptr;
  OT_TRY(ot_foreign_check(vm, who, v, expectedType, &payload));
  (void)payload;
  heap_finalize_foreign(&vm->heap, ref_get(vm, v).obj);
  return nil_v();
}
