// collections.c — compact dict, array and buffer mutators, structural
// equality/hashing. Spec 10.3, 10.5, 2.4; compact-dict layout per 2.7.
//
// This file works on heap internals: it derives interior pointers
// (TableEntry*, Value*) and re-derives them after every allocating call.
// Each stretch that holds one is annotated with why it cannot go stale.
#define OT_HEAP_INTERNALS
#include "collections.h"
#include "heap.h"
#include <math.h>

// ---------------------------------------------------------------------------
// equal? -- deep structural for pairs and arrays, identity for other mutables,
// type-strict. Cyclic arrays are not supported.

static bool value_equal(State* vm, Value a, Value b, bool structuralArrays) {
  for (;;) {
    if (a.tag != b.tag) return false;
    switch (a.tag) {
      case Tag_Nil:
      case Tag_Null:
      case Tag_False:
      case Tag_True:
      case Tag_Unwind: return true;
      case Tag_Int: return a.i == b.i;
      case Tag_Float:
        if (isnan(a.f) && isnan(b.f)) return true;
        return a.f == b.f;  // covers 0.0 == -0.0
      case Tag_Symbol:
      case Tag_Keyword: return a.id == b.id;
      case Tag_String: {
        StringData* sa = as_string(a);
        StringData* sb = as_string(b);
        if (sa->len != sb->len) return false;
        return memcmp(string_data_bytes(sa), string_data_bytes(sb), sa->len) == 0;
      }
      case Tag_Pair: {
        PairData* pa = as_pair(a);
        PairData* pb = as_pair(b);
        if (!value_equal(vm, pa->car, pb->car, structuralArrays)) return false;
        a = pa->cdr;
        b = pb->cdr;  // iterate on cdr
        break;
      }
      case Tag_Array: {
        if (a.obj == b.obj) return true;
        if (!structuralArrays) return false;
        if (as_array(a)->len != as_array(b)->len) return false;
        Value* ia = array_items(a);
        Value* ib = array_items(b);
        for (u32 i = 0; i < as_array(a)->len; i++)
          if (!value_equal(vm, ia[i], ib[i], true)) return false;
        return true;
      }
      default:  // other mutables + functions/macros/params/restarts: identity
        return a.obj == b.obj;
    }
  }
}

bool val_equal(State* vm, Value a, Value b) { return value_equal(vm, a, b, true); }

// Mutable table keys retain identity semantics even where user-facing equal?
// is structural. This keeps their hashes stable as their contents change.
static bool key_equal(State* vm, Value a, Value b) { return value_equal(vm, a, b, false); }

// ---------------------------------------------------------------------------
// Hash with table-key semantics.

static inline u64 mix64(u64 x) {  // splitmix64 finalizer
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

u64 val_hash(State* vm, Value v) {
  u64 seed = 0xA0761D64ull + (u64)v.tag * 0x9E3779B97F4A7C15ull;
  switch (v.tag) {
    case Tag_Nil:
    case Tag_Null:
    case Tag_False:
    case Tag_True:
    case Tag_Unwind: return mix64(seed);
    case Tag_Int: return mix64(seed ^ (u64)v.i);
    case Tag_Float: {
      f64 f = v.f;
      if (isnan(f)) return mix64(seed ^ 0x7FF8DEADBEEFull);  // all NaNs equal
      u64 bits;
      if (f == 0.0) bits = 0;  // normalize -0.0 to 0.0
      else memcpy(&bits, &f, sizeof bits);
      return mix64(seed ^ bits);
    }
    case Tag_Symbol:
    case Tag_Keyword: return mix64(seed ^ (u64)v.id);
    case Tag_String: {
      StringData* s = as_string(v);
      const char* p = string_data_bytes(s);
      u64 h = seed ^ 0xCBF29CE484222325ull;  // FNV-1a over bytes
      for (u32 i = 0; i < s->len; i++) h = (h ^ (u8)p[i]) * 0x100000001B3ull;
      return mix64(h);
    }
    case Tag_Pair: {
      PairData* p = as_pair(v);
      u64 hc = val_hash(vm, p->car);
      u64 hd = val_hash(vm, p->cdr);
      return mix64(seed ^ hc ^ ((hd << 17) | (hd >> 47)));
    }
    default:  // mutables: GC-stable identity, never an address
      return mix64(seed ^ (u64)heap_identity_of(&vm->heap, v.obj));
  }
}

// ---------------------------------------------------------------------------
// Pairs used as table keys freeze (their hash must stay stable), and pair
// mutation must not create cycles. Both walks are allocation-free; the
// C-heap worklists cannot go stale.

bool pair_key_frozen(Value pair) { return (pair.obj->flags & OBJ_PAIR_KEY) != 0; }

bool pair_contains(Value root, Value needle) {
  if (root.tag != Tag_Pair) return false;
  Obj* target = needle.obj;
  VecObjPtr pending = {0};
  VecObjPtr seen = {0};
  vec_push(&pending, root.obj);
  while (pending.len) {
    Obj* obj = vec_pop(&pending);
    if (obj == target) {
      vec_deinit(&pending);
      vec_deinit(&seen);
      return true;
    }
    bool known = false;
    for (u32 i = 0; i < seen.len; i++)
      if (seen.data[i] == obj) {
        known = true;
        break;
      }
    if (known) continue;
    vec_push(&seen, obj);
    PairData* pair = (PairData*)obj_payload(obj);
    if (pair->car.tag == Tag_Pair) vec_push(&pending, pair->car.obj);
    if (pair->cdr.tag == Tag_Pair) vec_push(&pending, pair->cdr.obj);
  }
  vec_deinit(&pending);
  vec_deinit(&seen);
  return false;
}

void freeze_pair_key(Value root) {
  if (root.tag != Tag_Pair) return;
  VecObjPtr pending = {0};
  vec_push(&pending, root.obj);
  while (pending.len) {
    Obj* obj = vec_pop(&pending);
    if (obj->flags & OBJ_PAIR_KEY) continue;
    obj->flags |= OBJ_PAIR_KEY;
    PairData* pair = (PairData*)obj_payload(obj);
    if (pair->car.tag == Tag_Pair) vec_push(&pending, pair->car.obj);
    if (pair->cdr.tag == Tag_Pair) vec_push(&pending, pair->cdr.obj);
  }
  vec_deinit(&pending);
}

// ---------------------------------------------------------------------------
// THE COMPACT DICT.
// Insertion-ordered entry vector {hash,key,value} + open-addressed index
// array whose slot width scales with capacity (u8/u16/u32). Slot value 0 is
// empty; otherwise entryIndex+1. Deletes tombstone the entry (key = Unwind
// sentinel) and leave the index slot claimed until the next rebuild; the
// table compacts when tombstones outnumber live entries.

static inline bool is_tomb(const TableEntry* e) { return e->key.tag == Tag_Unwind; }

// The index and entry arrays are GC objects, so every helper below takes the
// table through a rooted handle and re-derives its pointers after anything that
// can allocate. Nothing here may cache a TableEntry* or u8* across a make_*.

static inline u32 idx_get(Value table, u32 slot) {
  const u8* index = table_index(table);
  switch (as_table(table)->indexWidth) {
    case 1: return index[slot];
    case 2: return ((const u16*)index)[slot];
    default: return ((const u32*)index)[slot];
  }
}
static inline void idx_set(Value table, u32 slot, u32 v) {
  u8* index = table_index(table);
  switch (as_table(table)->indexWidth) {
    case 1: index[slot] = (u8)v; break;
    case 2: ((u16*)index)[slot] = (u16)v; break;
    default: ((u32*)index)[slot] = v; break;
  }
}

static u32 table_index_width(u32 entriesCap) {
  return entriesCap < 0xFF ? 1 : entriesCap < 0xFFFF ? 2 : 4;
}

// Rebuild the index for the current entries at capacity `cap` (pow2).
static void table_rebuild_index(State* vm, Ref table, u32 cap) {
  u32 width = table_index_width(as_table(ref_get(vm, table))->entriesCap);
  if (cap > UINT32_MAX / width) ot_fatal("table: index size overflow");
  // heap_alloc zeroes object payloads, so the fresh index starts all-empty.
  Value fresh = make_bytes(vm, cap * width);
  TableData* t = as_table(ref_get(vm, table));
  t->index = fresh;
  t->indexCap = cap;
  t->indexWidth = width;
  // No allocation past this point, so the entry pointer is stable.
  TableEntry* entries = table_entries(ref_get(vm, table));
  for (u32 i = 0; i < t->entriesLen; i++) {
    if (is_tomb(&entries[i])) continue;
    u32 slot = (u32)(entries[i].hash & (cap - 1));
    while (idx_get(ref_get(vm, table), slot) != 0) slot = (slot + 1) & (cap - 1);
    idx_set(ref_get(vm, table), slot, i + 1);
  }
}

// Drop tombstones (preserving order) and rebuild the index.
static void table_compact(State* vm, Ref table) {
  TableData* t = as_table(ref_get(vm, table));
  TableEntry* entries = table_entries(ref_get(vm, table));
  u32 w = 0;
  for (u32 i = 0; i < t->entriesLen; i++)
    if (!is_tomb(&entries[i])) entries[w++] = entries[i];
  t->entriesLen = w;
  t->tombstones = 0;
  u32 indexCap = t->indexCap;
  table_rebuild_index(vm, table, indexCap);  // t and entries are stale after this
}

// keep index load factor <= 3/4 (claimed slots = entriesLen incl tombstones)
static inline bool index_overloaded(u32 need, u32 cap) {
  return ((u64)need + 1u) * 4u > (u64)cap * 3u;
}

static void table_ensure(State* vm, Ref table, u32 extra) {
  TableData* t = as_table(ref_get(vm, table));
  if (extra > UINT32_MAX - t->entriesLen) ot_fatal("table: capacity overflow");
  u32 need = t->entriesLen + extra;
  if (need > t->entriesCap) {
    u32 ncap = grow_capacity(t->entriesCap, need, "table: capacity overflow");
    Value grown = make_entries(vm, ncap);
    t = as_table(ref_get(vm, table));  // re-derive: make_entries may have moved it
    TableEntry* dst = entries_items(grown);
    TableEntry* src = table_entries(ref_get(vm, table));
    for (u32 i = 0; i < t->entriesLen; i++) dst[i] = src[i];
    t->entries = grown;
    t->entriesCap = ncap;
    // entriesCap growth may bump the needed index width — rebuild if so.
    u32 width = table_index_width(ncap);
    if (!is_nil(t->index) && width != t->indexWidth) {
      u32 indexCap = t->indexCap;
      table_rebuild_index(vm, table, indexCap);
    }
  }
  t = as_table(ref_get(vm, table));
  if (is_nil(t->index) || index_overloaded(need, t->indexCap)) {
    u32 cap = t->indexCap ? t->indexCap : 8;
    while (index_overloaded(need, cap)) {
      if (cap > UINT32_MAX / 2) ot_fatal("table: capacity overflow");
      cap *= 2;
    }
    table_rebuild_index(vm, table, cap);
  }
}

// Find live entry index for key, or -1. Allocation-free.
static i64 table_find(State* vm, Value table, u64 hash, Value key) {
  TableData* t = as_table(table);
  if (is_nil(t->index) || t->indexCap == 0) return -1;
  TableEntry* entries = table_entries(table);
  u32 slot = (u32)(hash & (t->indexCap - 1));
  for (;;) {
    u32 e = idx_get(table, slot);
    if (e == 0) return -1;
    TableEntry* ent = &entries[e - 1];
    if (!is_tomb(ent) && ent->hash == hash && key_equal(vm, ent->key, key)) return (i64)(e - 1);
    slot = (slot + 1) & (t->indexCap - 1);
  }
}

Value table_get(State* vm, Value table, Value key) {
  i64 e = table_find(vm, table, val_hash(vm, key), key);
  return e < 0 ? nil_v() : table_entries(table)[e].val;
}

void table_put(State* vm, Ref table, Ref key, Ref v) {
  u64 h = val_hash(vm, ref_get(vm, key));
  i64 e = table_find(vm, ref_get(vm, table), h, ref_get(vm, key));
  if (is_nil(ref_get(vm, v))) {  // storing nil deletes
    if (e >= 0) {
      TableData* t = as_table(ref_get(vm, table));
      TableEntry* entries = table_entries(ref_get(vm, table));
      entries[e].key = unwind_v();  // tombstone
      entries[e].val = nil_v();
      t->count--;
      t->tombstones++;
      if (t->tombstones > t->count) table_compact(vm, table);
    }
    return;
  }
  if (e >= 0) {  // update keeps position
    table_entries(ref_get(vm, table))[e].val = ref_get(vm, v);
    return;
  }
  freeze_pair_key(ref_get(vm, key));
  table_ensure(vm, table, 1);  // insert (or re-insert) at the end; allocates
  // Everything below is derived after the last allocation.
  TableData* t = as_table(ref_get(vm, table));
  TableEntry* entries = table_entries(ref_get(vm, table));
  u32 idx = t->entriesLen++;
  entries[idx].hash = h;
  entries[idx].key = ref_get(vm, key);
  entries[idx].val = ref_get(vm, v);
  u32 slot = (u32)(h & (t->indexCap - 1));
  while (idx_get(ref_get(vm, table), slot) != 0) slot = (slot + 1) & (t->indexCap - 1);
  idx_set(ref_get(vm, table), slot, idx + 1);
  t->count++;
}

// The immediate variants reject heap values outright: an immediate needs no
// rooting, but letting a raw heap Value through this door would recreate the
// stale-copy bug the Ref signatures exist to prevent.
void table_put_iv(State* vm, Ref table, Value immKey, Ref v) {
  if (is_heap(immKey)) ot_fatal("table_put_iv: key must be an immediate");
  OT_SCOPE(vm);
  Ref k = ref_push(vm, immKey);
  table_put(vm, table, k, v);
}

void table_put_ii(State* vm, Ref table, Value immKey, Value immVal) {
  if (is_heap(immKey) || is_heap(immVal)) ot_fatal("table_put_ii: immediates required");
  OT_SCOPE(vm);
  Ref k = ref_push(vm, immKey);
  Ref v = ref_push(vm, immVal);
  table_put(vm, table, k, v);
}

u32 table_entry_count(Value table) { return as_table(table)->count; }

// Advance through the insertion-order storage, skipping tombstones. The cursor
// is a storage position rather than a live-entry index, so a complete traversal
// examines every stored entry at most once.
bool table_iter_next(Value table, u32* cursor, Value* k, Value* v) {
  TableData* t = as_table(table);
  TableEntry* entries = table_entries(table);
  while (*cursor < t->entriesLen) {
    TableEntry* entry = &entries[(*cursor)++];
    if (is_tomb(entry)) continue;
    *k = entry->key;
    *v = entry->val;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Arrays and buffers.

Value array_get(Value arr, i64 idx) {
  ArrayData* a = as_array(arr);
  if (idx < 0 || (u64)idx >= a->len) return nil_v();
  return array_items(arr)[idx];
}

void array_reserve(State* vm, Ref arr, u32 n) { array_reserve_h(&vm->heap, ref_get(vm, arr), n); }

void array_push(State* vm, Ref arr, Ref v) {
  u32 len = as_array(ref_get(vm, arr))->len;
  if (len == array_cap(ref_get(vm, arr))) array_reserve(vm, arr, len ? len * 2 : 8);
  // Derived after the last allocation.
  array_items(ref_get(vm, arr))[len] = ref_get(vm, v);
  as_array(ref_get(vm, arr))->len = len + 1;
}

void array_push_im(State* vm, Ref arr, Value imm) {
  if (is_heap(imm)) ot_fatal("array_push_im: immediate required");
  OT_SCOPE(vm);
  Ref v = ref_push(vm, imm);
  array_push(vm, arr, v);
}

void buffer_append(State* vm, Ref buffer, const char* src, u32 n) {
  buffer_append_h(&vm->heap, ref_get(vm, buffer), src, n);
}
