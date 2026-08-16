// builtins/data.cpp — compact dict, structural equality/hashing, data natives.
// Spec 10.3, 10.5, 2.4; compact-dict layout per 2.7.
#include "../builtins.hpp"
#include "../heap.hpp"  // Obj, PairData, StringData, ArrayData, as_* accessors, make_*
#include "../vm.hpp"    // Vm, raise_error
#include "../ns.hpp"
#include "../eval.hpp"  // apply() for update!
#include <cmath>

namespace ot {

// ---------------------------------------------------------------------------
// equal? — deep structural for immutables, identity for mutables, type-strict.
// NaN == NaN and 0.0 == -0.0 (for table keys).

bool val_equal(Vm& vm, Value a, Value b) {
  for (;;) {
    if (a.tag != b.tag) return false;
    switch (a.tag) {
      case Tag::Nil:
      case Tag::Null:
      case Tag::False:
      case Tag::True:
      case Tag::Unwind: return true;
      case Tag::Int: return a.i == b.i;
      case Tag::Float:
        if (std::isnan(a.f) && std::isnan(b.f)) return true;
        return a.f == b.f;  // covers 0.0 == -0.0
      case Tag::Symbol:
      case Tag::Keyword: return a.id == b.id;
      case Tag::String: {
        StringData* sa = as_string(a);
        StringData* sb = as_string(b);
        if (sa->len != sb->len) return false;
        return memcmp(string_bytes(sa), string_bytes(sb), sa->len) == 0;
      }
      case Tag::Pair: {
        PairData* pa = as_pair(a);
        PairData* pb = as_pair(b);
        if (!val_equal(vm, pa->car, pb->car)) return false;
        a = pa->cdr;
        b = pb->cdr;  // iterate on cdr
        break;
      }
      default:  // mutables + functions/macros/params/restarts: identity
        return a.obj == b.obj;
    }
  }
}

// ---------------------------------------------------------------------------
// Structural hash with equal? semantics.

static inline u64 mix64(u64 x) {  // splitmix64 finalizer
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

u64 val_hash(Vm& vm, Value v) {
  u64 seed = 0xA0761D64ull + (u64)v.tag * 0x9E3779B97F4A7C15ull;
  switch (v.tag) {
    case Tag::Nil:
    case Tag::Null:
    case Tag::False:
    case Tag::True:
    case Tag::Unwind: return mix64(seed);
    case Tag::Int: return mix64(seed ^ (u64)v.i);
    case Tag::Float: {
      f64 f = v.f;
      if (std::isnan(f)) return mix64(seed ^ 0x7FF8DEADBEEFull);  // all NaNs equal
      u64 bits;
      if (f == 0.0) bits = 0;  // normalize -0.0 to 0.0
      else memcpy(&bits, &f, sizeof bits);
      return mix64(seed ^ bits);
    }
    case Tag::Symbol:
    case Tag::Keyword: return mix64(seed ^ (u64)v.id);
    case Tag::String: {
      StringData* s = as_string(v);
      const char* p = string_bytes(s);
      u64 h = seed ^ 0xCBF29CE484222325ull;  // FNV-1a over bytes
      for (u32 i = 0; i < s->len; i++) h = (h ^ (u8)p[i]) * 0x100000001B3ull;
      return mix64(h);
    }
    case Tag::Pair: {
      PairData* p = as_pair(v);
      u64 hc = val_hash(vm, p->car);
      u64 hd = val_hash(vm, p->cdr);
      return mix64(seed ^ hc ^ ((hd << 17) | (hd >> 47)));
    }
    default:  // mutables: GC-stable identity, never an address
      return mix64(seed ^ (u64)vm.heap.identityOf(v.obj));
  }
}

// ---------------------------------------------------------------------------
// THE COMPACT DICT.
// Insertion-ordered entry vector {hash,key,value} + open-addressed index
// array whose slot width scales with capacity (u8/u16/u32). Slot value 0 is
// empty; otherwise entryIndex+1. Deletes tombstone the entry (key = Unwind
// sentinel) and leave the index slot claimed until the next rebuild; the
// table compacts when tombstones outnumber live entries.

static const Value TOMB = unwind_v();
static inline bool is_tomb(const TableEntry& e) { return e.key.tag == Tag::Unwind; }

static bool pair_graph_contains(Value root, Obj* needle) {
  if (root.tag != Tag::Pair) return false;
  Vec<Obj*> pending;
  Vec<Obj*> seen;
  pending.push(root.obj);
  while (pending.len) {
    Obj* obj = pending.pop();
    if (obj == needle) return true;
    bool known = false;
    for (u32 i = 0; i < seen.len; i++)
      if (seen[i] == obj) {
        known = true;
        break;
      }
    if (known) continue;
    seen.push(obj);
    PairData* pair = (PairData*)obj_payload(obj);
    if (pair->car.tag == Tag::Pair) pending.push(pair->car.obj);
    if (pair->cdr.tag == Tag::Pair) pending.push(pair->cdr.obj);
  }
  return false;
}

static void freeze_pair_key(Value root) {
  if (root.tag != Tag::Pair) return;
  Vec<Obj*> pending;
  pending.push(root.obj);
  while (pending.len) {
    Obj* obj = pending.pop();
    if (obj->flags & OBJ_PAIR_KEY) continue;
    obj->flags |= OBJ_PAIR_KEY;
    PairData* pair = (PairData*)obj_payload(obj);
    if (pair->car.tag == Tag::Pair) pending.push(pair->car.obj);
    if (pair->cdr.tag == Tag::Pair) pending.push(pair->cdr.obj);
  }
}

static inline u32 idx_get(TableData* t, u32 slot) {
  switch (t->indexWidth) {
    case 1: return t->index[slot];
    case 2: return ((u16*)t->index)[slot];
    default: return ((u32*)t->index)[slot];
  }
}
static inline void idx_set(TableData* t, u32 slot, u32 v) {
  switch (t->indexWidth) {
    case 1: t->index[slot] = (u8)v; break;
    case 2: ((u16*)t->index)[slot] = (u16)v; break;
    default: ((u32*)t->index)[slot] = v; break;
  }
}

static u32 table_index_width(u32 entriesCap) {
  return entriesCap < 0xFF ? 1 : entriesCap < 0xFFFF ? 2 : 4;
}

// Rebuild the index for the current entries array at capacity `cap` (pow2).
static void table_rebuild_index(TableData* t, u32 cap) {
  free(t->index);
  u32 width = table_index_width(t->entriesCap);
  t->index = (u8*)calloc((size_t)cap, width);
  if (!t->index) ot_fatal("table: out of memory");
  t->indexCap = cap;
  t->indexWidth = width;
  for (u32 i = 0; i < t->entriesLen; i++) {
    if (is_tomb(t->entries[i])) continue;
    u32 slot = (u32)(t->entries[i].hash & (cap - 1));
    while (idx_get(t, slot) != 0) slot = (slot + 1) & (cap - 1);
    idx_set(t, slot, i + 1);
  }
}

// Drop tombstones (preserving order) and rebuild the index.
static void table_compact(TableData* t) {
  u32 w = 0;
  for (u32 i = 0; i < t->entriesLen; i++)
    if (!is_tomb(t->entries[i])) t->entries[w++] = t->entries[i];
  t->entriesLen = w;
  t->tombstones = 0;
  table_rebuild_index(t, t->indexCap);
}

static void table_ensure(TableData* t, u32 extra) {
  if (extra > UINT32_MAX - t->entriesLen) ot_fatal("table: capacity overflow");
  u32 need = t->entriesLen + extra;
  if (need > t->entriesCap) {
    u32 ncap = t->entriesCap ? t->entriesCap : 8;
    while (ncap < need) {
      if (ncap > UINT32_MAX / 2) ot_fatal("table: capacity overflow");
      ncap *= 2;
    }
    TableEntry* ne = (TableEntry*)realloc(t->entries, (size_t)ncap * sizeof(TableEntry));
    if (!ne) ot_fatal("table: out of memory");
    t->entries = ne;
    t->entriesCap = ncap;
    // entriesCap growth may bump the needed index width — rebuild if so.
    u32 width = table_index_width(ncap);
    if (t->index && width != t->indexWidth) table_rebuild_index(t, t->indexCap);
  }
  // keep index load factor <= 3/4 (claimed slots = entriesLen incl tombstones)
  auto index_overloaded = [need](u32 cap) { return ((u64)need + 1u) * 4u > (u64)cap * 3u; };
  if (!t->index || index_overloaded(t->indexCap)) {
    u32 cap = t->indexCap ? t->indexCap : 8;
    while (index_overloaded(cap)) {
      if (cap > UINT32_MAX / 2) ot_fatal("table: capacity overflow");
      cap *= 2;
    }
    table_rebuild_index(t, cap);
  }
}

// Find live entry index for key, or -1.
static i64 table_find(Vm& vm, TableData* t, u64 hash, Value key) {
  if (!t->index || t->indexCap == 0) return -1;
  u32 slot = (u32)(hash & (t->indexCap - 1));
  for (;;) {
    u32 e = idx_get(t, slot);
    if (e == 0) return -1;
    TableEntry& ent = t->entries[e - 1];
    if (!is_tomb(ent) && ent.hash == hash && val_equal(vm, ent.key, key)) return (i64)(e - 1);
    slot = (slot + 1) & (t->indexCap - 1);
  }
}

Value table_get(Vm& vm, Value table, Value key) {
  TableData* t = as_table(table);
  i64 e = table_find(vm, t, val_hash(vm, key), key);
  return e < 0 ? nil_v() : t->entries[e].val;
}

Value table_put(Vm& vm, Value table, Value key, Value v) {
  TableData* t = as_table(table);
  u64 h = val_hash(vm, key);
  i64 e = table_find(vm, t, h, key);
  if (is_nil(v)) {  // storing nil deletes
    if (e >= 0) {
      t->entries[e].key = TOMB;
      t->entries[e].val = nil_v();
      t->count--;
      t->tombstones++;
      if (t->tombstones > t->count) table_compact(t);
    }
    return table;
  }
  if (e >= 0) {  // update keeps position
    t->entries[e].val = v;
    return table;
  }
  freeze_pair_key(key);
  table_ensure(t, 1);  // insert (or re-insert) at the end
  u32 idx = t->entriesLen++;
  t->entries[idx].hash = h;
  t->entries[idx].key = key;
  t->entries[idx].val = v;
  u32 slot = (u32)(h & (t->indexCap - 1));
  while (idx_get(t, slot) != 0) slot = (slot + 1) & (t->indexCap - 1);
  idx_set(t, slot, idx + 1);
  t->count++;
  return table;
}

u32 table_entry_count(Value table) { return as_table(table)->count; }

// Advance through the insertion-order storage, skipping tombstones. The cursor
// is a storage position rather than a live-entry index, so a complete traversal
// examines every stored entry at most once.
bool table_iter_next(Value table, u32* cursor, Value* k, Value* v) {
  TableData* t = as_table(table);
  while (*cursor < t->entriesLen) {
    TableEntry& entry = t->entries[(*cursor)++];
    if (is_tomb(entry)) continue;
    *k = entry.key;
    *v = entry.val;
    return true;
  }
  return false;
}

// Strong definition for the printer's table hook (weak no-op in printer.cpp).
bool printer_table_next(Vm&, Value table, u32* cursor, Value* k, Value* v) {
  return table_iter_next(table, cursor, k, v);
}

// ---------------------------------------------------------------------------
// Arrays.

Value array_get(Value arr, i64 idx) {
  ArrayData* a = as_array(arr);
  if (idx < 0 || (u64)idx >= a->len) return nil_v();
  return a->items[idx];
}

void array_push(Vm&, Value arr, Value v) {
  ArrayData* a = as_array(arr);
  if (a->len == a->cap) {
    if (a->cap > UINT32_MAX / 2) ot_fatal("array: capacity overflow");
    u32 ncap = a->cap ? a->cap * 2 : 8;
    Value* ni = (Value*)realloc(a->items, (size_t)ncap * sizeof(Value));
    if (!ni) ot_fatal("array: out of memory");
    a->items = ni;
    a->cap = ncap;
  }
  a->items[a->len++] = v;
}

// ---------------------------------------------------------------------------
// Natives.

static Value nat_cons(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "cons", argc, 2, 2));
  return make_pair(vm, ARG(0), ARG(1));
}

static Value nat_car(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "car", argc, 1, 1));
  OT_TRY(need_pair(vm, "car", ARG(0)));
  return as_pair(ARG(0))->car;
}
static Value nat_cdr(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "cdr", argc, 1, 1));
  OT_TRY(need_pair(vm, "cdr", ARG(0)));
  return as_pair(ARG(0))->cdr;
}
static Value set_pair_field(Vm& vm, u32 base, u32 argc, const char* who, bool car) {
  OT_TRY(need_argc(vm, who, argc, 2, 2));
  OT_TRY(need_pair(vm, who, ARG(0)));
  if (ARG(0).obj->flags & OBJ_PAIR_KEY)
    return raise_error(vm, "%s: cannot mutate a pair used as a table key", who);
  if (pair_graph_contains(ARG(1), ARG(0).obj))
    return raise_error(vm, "%s: cyclic pair structure is not supported", who);
  if (car) as_pair(ARG(0))->car = ARG(1);
  else as_pair(ARG(0))->cdr = ARG(1);
  return ARG(0);
}
static Value nat_set_car(Vm& vm, u32 base, u32 argc) {
  return set_pair_field(vm, base, argc, "set-car!", true);
}
static Value nat_set_cdr(Vm& vm, u32 base, u32 argc) {
  return set_pair_field(vm, base, argc, "set-cdr!", false);
}
static Value nat_caar(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "caar", argc, 1, 1));
  OT_TRY(need_pair(vm, "caar", ARG(0)));
  OT_TRY(need_pair(vm, "caar", as_pair(ARG(0))->car));
  return as_pair(as_pair(ARG(0))->car)->car;
}
static Value nat_cadr(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "cadr", argc, 1, 1));
  OT_TRY(need_pair(vm, "cadr", ARG(0)));
  OT_TRY(need_pair(vm, "cadr", as_pair(ARG(0))->cdr));
  return as_pair(as_pair(ARG(0))->cdr)->car;
}
static Value nat_cddr(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "cddr", argc, 1, 1));
  OT_TRY(need_pair(vm, "cddr", ARG(0)));
  OT_TRY(need_pair(vm, "cddr", as_pair(ARG(0))->cdr));
  return as_pair(as_pair(ARG(0))->cdr)->cdr;
}

static Value nat_list(Vm& vm, u32 base, u32 argc) {
  Scope s(vm);
  Slot acc = s.push(null_v());
  for (u32 i = argc; i-- > 0;) {
    Value cell = make_pair(vm, ARG(i), acc.get());
    acc.set(cell);
  }
  return acc.get();
}

static Value nat_append(Vm& vm, u32 base, u32 argc) {
  Scope s(vm);
  Slot acc = s.push(null_v());
  for (u32 i = argc; i-- > 0;) {
    // reverse the i-th list onto a temp, then cons onto acc
    Value lst = ARG(i);
    if (lst.tag != Tag::Null && lst.tag != Tag::Pair)
      return raise_error(vm, "append: expected proper list");
    // collect elements onto the VM stack (a GC root — a C++ Vec's copies
    // would go stale when make_pair below collects)
    u32 ebase = vm.stack.len;
    for (Value p = lst; p.tag != Tag::Null;) {  // no allocation in this walk
      if (p.tag != Tag::Pair) return raise_error(vm, "append: improper list");
      vm.push(as_pair(p)->car);
      p = as_pair(p)->cdr;
    }
    for (u32 j = vm.stack.len; j-- > ebase;) {
      Value cell = make_pair(vm, vm.stack[j], acc.get());
      acc.set(cell);
    }
    vm.popTo(ebase);
  }
  return acc.get();
}

static Value nat_length(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "length", argc, 1, 1));
  Value v = ARG(0);
  switch (v.tag) {
    case Tag::Null: return int_v(0);
    case Tag::Pair: {
      i64 n = 0;
      for (Value p = v; p.tag != Tag::Null; p = as_pair(p)->cdr) {
        if (p.tag != Tag::Pair) return raise_error(vm, "length: improper list");
        n++;
      }
      return int_v(n);
    }
    case Tag::Array: return int_v((i64)as_array(v)->len);
    case Tag::Table: return int_v((i64)as_table(v)->count);
    case Tag::String: return int_v((i64)as_string(v)->nchars);
    case Tag::Buffer: {
      BufferData* b = as_buffer(v);
      return int_v((i64)utf8_count(b->buf.data, b->buf.len));
    }
    default: return raise_error(vm, "length: unsupported type");
  }
}

static Value nat_reverse(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "reverse", argc, 1, 1));
  Value v = ARG(0);
  if (is_nil(v)) return nil_v();  // kind-preserving: nothing to preserve
  if (v.tag == Tag::Null) return null_v();
  if (v.tag == Tag::Pair) {
    Scope s(vm);
    Slot acc = s.push(null_v());
    Slot p = s.push(v);  // cursor, rooted across make_pair
    while (p.get().tag != Tag::Null) {
      if (p.get().tag != Tag::Pair) return raise_error(vm, "reverse: improper list");
      Value cell = make_pair(vm, as_pair(p.get())->car, acc.get());
      acc.set(cell);
      p.set(as_pair(p.get())->cdr);
    }
    return acc.get();
  }
  if (v.tag == Tag::Array) {
    Scope s(vm);
    Slot out = s.push(make_array(vm, as_array(ARG(0))->len));
    // re-read: make_array collected; cached across array_push only (alloc-free)
    ArrayData* src = as_array(ARG(0));
    for (u32 i = src->len; i-- > 0;) array_push(vm, out.get(), src->items[i]);
    return out.get();
  }
  return raise_error(vm, "reverse: expected sequence");
}

static Value nat_list_to_array(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "list->array", argc, 1, 1));
  Value v = ARG(0);
  if (v.tag != Tag::Null && v.tag != Tag::Pair)
    return raise_error(vm, "list->array: expected list");
  Scope s(vm);
  Slot out = s.push(make_array(vm, 8));
  // re-read the list from its rooted arg slot: make_array collected
  for (Value p = ARG(0); p.tag != Tag::Null; p = as_pair(p)->cdr) {
    if (p.tag != Tag::Pair) return raise_error(vm, "list->array: improper list");
    array_push(vm, out.get(), as_pair(p)->car);  // no GC allocation in this loop
  }
  return out.get();
}

static Value nat_array_to_list(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "array->list", argc, 1, 1));
  OT_TRY(need_array(vm, "array->list", ARG(0)));
  Scope s(vm);
  Slot acc = s.push(null_v());
  // re-read the array through its rooted arg slot every iteration: each
  // make_pair can collect and move it
  for (u32 i = as_array(ARG(0))->len; i-- > 0;) {
    Value cell = make_pair(vm, as_array(ARG(0))->items[i], acc.get());
    acc.set(cell);
  }
  return acc.get();
}

static Value nat_array(Vm& vm, u32 base, u32 argc) {
  Scope s(vm);
  Slot out = s.push(make_array(vm, argc ? argc : 4));
  for (u32 i = 0; i < argc; i++) array_push(vm, out.get(), ARG(i));
  return out.get();
}

static Value nat_table(Vm& vm, u32 base, u32 argc) {
  if (argc % 2 != 0) return raise_error(vm, "table: odd argument count");
  Scope s(vm);
  Slot t = s.push(make_table(vm));
  for (u32 i = 0; i < argc; i += 2) table_put(vm, t.get(), ARG(i), ARG(i + 1));
  return t.get();
}

// String code-point index -> one-character string, or nil.
static Value string_char_at(Vm& vm, Value s, i64 idx) {
  StringData* sd = as_string(s);
  if (idx < 0 || (u64)idx >= sd->nchars) return nil_v();
  const char* p = string_bytes(sd);
  u32 i = 0;
  i64 c = -1;
  u32 start = 0;
  for (; i < sd->len; i++) {
    if (((u8)p[i] & 0xC0) != 0x80) {
      c++;
      if (c == idx) start = i;
      else if (c == idx + 1) break;
    }
  }
  u32 end = (c == idx) ? sd->len : i;
  if (c < idx) return nil_v();
  return make_string_from(vm, s, start, end - start);
}

static Value do_get(Vm& vm, Value coll, Value key, Value dflt) {
  Value r = nil_v();
  switch (coll.tag) {
    case Tag::Nil: break;                                  // miss
    case Tag::Table: r = table_get(vm, coll, key); break;  // alloc-free
    case Tag::Array:
      if (key.tag == Tag::Int) r = array_get(coll, key.i);
      break;
    case Tag::String:
      if (key.tag == Tag::Int) {
        // string_char_at allocates — root dflt across it
        Scope s(vm);
        Slot dS = s.push(dflt);
        r = string_char_at(vm, coll, key.i);
        return is_nil(r) ? dS.get() : r;
      }
      break;
    default: return raise_error(vm, "get: unsupported collection type");
  }
  return is_nil(r) ? dflt : r;
}

static Value nat_get(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "get", argc, 2, 3));
  return do_get(vm, ARG(0), ARG(1), argc == 3 ? ARG(2) : nil_v());
}

static Value do_put(Vm& vm, Value coll, Value k, Value v) {
  if (coll.tag == Tag::Table) {
    table_put(vm, coll, k, v);
    return coll;
  }
  if (coll.tag == Tag::Array) {
    if (k.tag != Tag::Int) return raise_error(vm, "put!: array index must be an int");
    ArrayData* a = as_array(coll);
    if (k.i < 0 || (u64)k.i >= a->len) return raise_error(vm, "put!: array index out of range");
    a->items[k.i] = v;
    return coll;
  }
  return raise_error(vm, "put!: expected table or array");
}

static Value nat_put(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "put!", argc, 3, UINT32_MAX));
  if ((argc - 1) % 2 != 0) return raise_error(vm, "put!: expected coll plus key/value pairs");
  for (u32 i = 1; i < argc; i += 2) OT_TRY(do_put(vm, ARG(0), ARG(i), ARG(i + 1)));
  return ARG(0);
}

static Value nat_push(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "push!", argc, 1, UINT32_MAX));
  OT_TRY(need_array(vm, "push!", ARG(0)));
  for (u32 i = 1; i < argc; i++) array_push(vm, ARG(0), ARG(i));
  return ARG(0);
}

static Value nat_pop(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "pop!", argc, 1, 1));
  OT_TRY(need_array(vm, "pop!", ARG(0)));
  ArrayData* a = as_array(ARG(0));
  if (a->len == 0) return nil_v();
  return a->items[--a->len];
}

static Value nat_update(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "update!", argc, 3, UINT32_MAX));
  Value cur;
  OT_TRY(cur = do_get(vm, ARG(0), ARG(1), nil_v()));
  Scope s(vm);
  u32 cbase = vm.push(cur);
  for (u32 i = 3; i < argc; i++) vm.push(ARG(i));
  Value nv = apply(vm, ARG(2), cbase, argc - 2);  // re-read fn: do_get may have collected
  OT_TRY(nv);
  OT_TRY(do_put(vm, ARG(0), ARG(1), nv));  // re-read; do_put is alloc-free with nv
  return ARG(0);
}

static Value nat_keys(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "keys", argc, 1, 1));
  if (is_nil(ARG(0))) return make_array(vm, 8);
  if (ARG(0).tag != Tag::Table) return raise_error(vm, "keys: expected table");
  Scope s(vm);
  Slot out = s.push(make_array(vm, 8));
  TableData* t = as_table(ARG(0));  // cached across array_push only (alloc-free)
  for (u32 i = 0; i < t->entriesLen; i++)
    if (!is_tomb(t->entries[i])) array_push(vm, out.get(), t->entries[i].key);
  return out.get();
}

static Value nat_values(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "values", argc, 1, 1));
  if (is_nil(ARG(0))) return make_array(vm, 8);
  if (ARG(0).tag != Tag::Table) return raise_error(vm, "values: expected table");
  Scope s(vm);
  Slot out = s.push(make_array(vm, 8));
  TableData* t = as_table(ARG(0));  // cached across array_push only (alloc-free)
  for (u32 i = 0; i < t->entriesLen; i++)
    if (!is_tomb(t->entries[i])) array_push(vm, out.get(), t->entries[i].val);
  return out.get();
}

static Value nat_copy(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "copy", argc, 1, 1));
  Value v = ARG(0);
  if (is_nil(v)) return nil_v();  // kind-preserving over absence
  if (v.tag == Tag::Array) {
    Scope s(vm);
    Slot out = s.push(make_array(vm, as_array(v)->len));
    // re-read: make_array collected; cached across array_push only (alloc-free)
    ArrayData* a = as_array(ARG(0));
    for (u32 i = 0; i < a->len; i++) array_push(vm, out.get(), a->items[i]);
    return out.get();
  }
  if (v.tag == Tag::Table) {
    Scope s(vm);
    Slot out = s.push(make_table(vm));
    // re-read: make_table collected; cached across table_put only (alloc-free)
    TableData* t = as_table(ARG(0));
    for (u32 i = 0; i < t->entriesLen; i++)
      if (!is_tomb(t->entries[i])) table_put(vm, out.get(), t->entries[i].key, t->entries[i].val);
    return out.get();
  }
  return raise_error(vm, "copy: expected array, table, or nil");
}

void register_data(Vm& vm) {
  def_native(vm, "cons", nat_cons);
  def_native(vm, "car", nat_car);
  def_native(vm, "cdr", nat_cdr);
  def_native(vm, "set-car!", nat_set_car);
  def_native(vm, "set-cdr!", nat_set_cdr);
  def_native(vm, "caar", nat_caar);
  def_native(vm, "cadr", nat_cadr);
  def_native(vm, "cddr", nat_cddr);
  def_native(vm, "list", nat_list);
  def_native(vm, "append", nat_append);
  def_native(vm, "length", nat_length);
  def_native(vm, "reverse", nat_reverse);
  def_native(vm, "list->array", nat_list_to_array);
  def_native(vm, "array->list", nat_array_to_list);
  def_native(vm, "array", nat_array);
  def_native(vm, "table", nat_table);
  def_native(vm, "get", nat_get);
  def_native(vm, "put!", nat_put);
  def_native(vm, "push!", nat_push);
  def_native(vm, "pop!", nat_pop);
  def_native(vm, "update!", nat_update);
  def_native(vm, "keys", nat_keys);
  def_native(vm, "values", nat_values);
  def_native(vm, "copy", nat_copy);
}

}  // namespace ot
