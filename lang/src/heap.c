// heap.c — semispace Cheney scavenger. See heap.h for the design notes.
#include "heap.h"

static void* foreign_payload(ForeignData* d) {
  if (!(d->flags & ForeignExternal)) return d + 1;
  void* payload = nullptr;
  memcpy(&payload, d + 1, sizeof payload);
  return payload;
}

static u32 align8(u32 n) {
  if (n > UINT32_MAX - 7u) ot_fatal("heap: size overflow");
  return (n + 7u) & ~7u;
}

static u32 obj_total_size(u32 payloadBytes) {
  u32 payloadSize = align8(payloadBytes);
  if (payloadSize > UINT32_MAX - (u32)sizeof(Obj)) ot_fatal("heap: size overflow");
  return (u32)sizeof(Obj) + payloadSize;
}

static u32 obj_total_size_of(Obj* o) { return obj_total_size(o->size); }

void heap_init(Heap* h, State* vm, u32 initialBytes, u32 maxBytes) {
  memset(h, 0, sizeof *h);
  h->vm = vm;
  h->spaceSize = initialBytes < 1024 ? 1024 : align8(initialBytes);
  h->used = 0;
  h->maxBytes = maxBytes;
  h->nextIdent = 1;
  h->collections = 0;
  h->toSpace = nullptr;
  h->toSize = 0;
  h->toUsed = 0;
  if (h->maxBytes < 1024 || h->spaceSize > h->maxBytes) ot_fatal("heap: invalid size limits");
  h->space = (char*)ot_alloc(h->spaceSize);
  if (!h->space) ot_fatal("heap: cannot allocate initial space");
}

void heap_deinit(Heap* h) {
  // Free C-heap storage owned by still-live finalizable objects.
  for (u32 i = 0; i < h->finalizable.len; i++) {
    Obj* o = h->finalizable.data[i];
    switch (o->type) {
      case ObjType_Array: ot_free(((ArrayData*)obj_payload(o))->items); break;
      case ObjType_Table: {
        TableData* td = (TableData*)obj_payload(o);
        ot_free(td->entries);
        ot_free(td->index);
        break;
      }
      case ObjType_Buffer: buf_deinit(&((BufferData*)obj_payload(o))->buf); break;
      case ObjType_Foreign: heap_finalize_foreign(h, o); break;
      case ObjType_Code: {
        CodeData* d = (CodeData*)obj_payload(o);
        ot_free(d->bytes);
        ot_free(d->consts);
        break;
      }
      default: break;
    }
  }
  ot_free(h->space);
  vec_deinit(&h->rootWalkers);
  vec_deinit(&h->tempRoots);
  vec_deinit(&h->finalizable);
  vec_deinit(&h->foreignTypes);
}

void heap_add_roots(Heap* h, RootWalkFn fn, void* ud) {
  vec_push(&h->rootWalkers, ((RootEntry){fn, ud}));
}

static void heap_collect_into(Heap* h, u32 newSize);

Obj* heap_alloc(Heap* h, ObjType t, u32 payloadBytes) {
  // Check the configured cap before alignment/header arithmetic can wrap.
  if (payloadBytes > h->maxBytes) ot_fatal("heap: allocation exceeds cap");
  u32 total = obj_total_size(payloadBytes);
  if (total > h->maxBytes) ot_fatal("heap: allocation exceeds cap");
#ifdef OT_GC_STRESS
  // Collect every OT_GC_STRESS_EVERY-th alloc (default 1 = every alloc).
  // The throttle keeps stress iteration tolerable on alloc-heavy tests;
  // the final gate runs at 1.
  static u32 stressEvery = 0;
  static u32 stressTick = 0;
  if (stressEvery == 0) {
    const char* e = getenv("OT_GC_STRESS_EVERY");
    stressEvery = e ? (u32)atoi(e) : 1;
    if (stressEvery == 0) stressEvery = 1;
  }
  if (++stressTick >= stressEvery) {
    stressTick = 0;
    heap_collect(h);
  }
#endif
  OT_ASSERT(h->used <= h->spaceSize);
  if (total > h->spaceSize - h->used) {
    heap_collect(h);
    while (total > h->spaceSize - h->used) {
      if (h->spaceSize > h->maxBytes / 2) ot_fatal("heap: out of memory (cap reached)");
      heap_collect_into(h, h->spaceSize * 2);
    }
  }
  Obj* o = (Obj*)(h->space + h->used);
  h->used += total;
  o->type = t;
  o->flags = 0;
  o->_pad = 0;
  o->size = payloadBytes;
  o->forward = nullptr;
  o->ident = 0;
  memset(obj_payload(o), 0, payloadBytes);
  if (t == ObjType_Array || t == ObjType_Table || t == ObjType_Buffer || t == ObjType_Code ||
      t == ObjType_Foreign)
    vec_push(&h->finalizable, o);
  return o;
}

static Obj* copy_obj(Heap* h, Obj* o) {
  if (o->forward) return o->forward;
  u32 total = obj_total_size_of(o);
  OT_ASSERT(h->toUsed + total <= h->toSize);
  Obj* n = (Obj*)(h->toSpace + h->toUsed);
  h->toUsed += total;
  memcpy(n, o, total);
  n->forward = nullptr;
  o->forward = n;
  return n;
}

static void visit_slot(Heap* h, Value* slot) {
  if (is_heap(*slot) && slot->obj) slot->obj = copy_obj(h, slot->obj);
}

static void visit_trampoline(void* ctx, Value* slot) { visit_slot((Heap*)ctx, slot); }

void heap_collect(Heap* h) { heap_collect_into(h, h->spaceSize); }

static void heap_collect_into(Heap* h, u32 newSize) {
  h->toSize = newSize;
  h->toSpace = (char*)ot_alloc(h->toSize);
  if (!h->toSpace) ot_fatal("heap: cannot allocate to-space");
  h->toUsed = 0;

  // 1. Copy roots (registered walkers + internal temp roots).
  for (u32 i = 0; i < h->rootWalkers.len; i++)
    h->rootWalkers.data[i].fn(h->rootWalkers.data[i].ud, visit_trampoline, h);
  for (u32 i = 0; i < h->tempRoots.len; i++) visit_slot(h, &h->tempRoots.data[i]);

  // 2. Cheney scan: walk to-space, tracing each copied object's fields.
  u32 scan = 0;
  while (scan < h->toUsed) {
    Obj* o = (Obj*)(h->toSpace + scan);
    void* p = obj_payload(o);
    switch (o->type) {
      case ObjType_Pair: {
        PairData* d = (PairData*)p;
        visit_slot(h, &d->car);
        visit_slot(h, &d->cdr);
        break;
      }
      case ObjType_Array: {
        ArrayData* d = (ArrayData*)p;
        for (u32 i = 0; i < d->len; i++) visit_slot(h, &d->items[i]);
        break;
      }
      case ObjType_Table: {
        TableData* d = (TableData*)p;
        for (u32 i = 0; i < d->entriesLen; i++) {
          if (d->entries[i].key.tag == Tag_Unwind) continue;  // tombstone
          visit_slot(h, &d->entries[i].key);
          visit_slot(h, &d->entries[i].val);
        }
        break;
      }
      case ObjType_Function:
      case ObjType_Macro: {
        FunctionData* d = (FunctionData*)p;
        visit_slot(h, &d->code);
        visit_slot(h, &d->nsName);
        visit_slot(h, &d->docstring);
        for (u32 i = 0; i < d->nupvals; i++) visit_slot(h, &function_upvals(d)[i]);
        break;
      }
      case ObjType_Code: {
        CodeData* d = (CodeData*)p;
        for (u32 i = 0; i < d->constCount; i++) visit_slot(h, &d->consts[i]);
        break;
      }
      case ObjType_Param: visit_slot(h, &((ParamData*)p)->defaultVal); break;
      case ObjType_Restart: visit_slot(h, &((RestartData*)p)->description); break;
      case ObjType_String:
      case ObjType_Buffer:
      case ObjType_Foreign: break;
    }
    scan += obj_total_size_of(o);
  }

  // 3. Sweep finalizable list: free C-heap storage of dead objects, update
  //    survivors to their new addresses.
  u32 keep = 0;
  for (u32 i = 0; i < h->finalizable.len; i++) {
    Obj* o = h->finalizable.data[i];
    if (o->forward) {
      h->finalizable.data[keep++] = o->forward;
    } else {
      void* p = obj_payload(o);
      switch (o->type) {
        case ObjType_Array: ot_free(((ArrayData*)p)->items); break;
        case ObjType_Table: {
          TableData* td = (TableData*)p;
          ot_free(td->entries);
          ot_free(td->index);
          break;
        }
        case ObjType_Buffer: buf_deinit(&((BufferData*)p)->buf); break;
        case ObjType_Foreign: heap_finalize_foreign(h, o); break;
        case ObjType_Code: {
          CodeData* d = (CodeData*)p;
          ot_free(d->bytes);
          ot_free(d->consts);
          break;
        }
        default: break;
      }
    }
  }
  h->finalizable.len = keep;

#ifdef OT_GC_STRESS
  // Poison from-space so any stale Value read across a collection fails
  // loudly instead of silently seeing the old payload.
  memset(h->space, 0xAB, h->spaceSize);
#endif
  ot_free(h->space);
  h->space = h->toSpace;
  h->spaceSize = h->toSize;
  h->used = h->toUsed;
  h->toSpace = nullptr;
  h->toSize = 0;
  h->toUsed = 0;
  h->collections++;

  // Grow policy: if live > 50% after the copy, double next time via an
  // immediate re-collect into a bigger space (cheap: live set is small).
  if (h->used > h->spaceSize / 2 && h->spaceSize <= h->maxBytes / 2)
    heap_collect_into(h, h->spaceSize * 2);
}

u32 heap_identity_of(Heap* h, Obj* o) {
  if (o->ident == 0) o->ident = h->nextIdent++;
  return o->ident;
}

u32 heap_add_foreign_type(Heap* h, u32 nameSym, ForeignFinalizer finalize) {
  for (u32 i = 0; i < h->foreignTypes.len; i++) {
    if (h->foreignTypes.data[i].nameSym != nameSym) continue;
    if (h->foreignTypes.data[i].finalize != finalize) ot_fatal("foreign type registered twice");
    return i + 1;
  }
  vec_push(&h->foreignTypes, ((ForeignType){nameSym, finalize}));
  return h->foreignTypes.len;
}

const ForeignType* heap_foreign_type(const Heap* h, u32 typeId) {
  if (typeId == 0 || typeId > h->foreignTypes.len) return nullptr;
  return &h->foreignTypes.data[typeId - 1];
}

void heap_finalize_foreign(Heap* h, Obj* o) {
  OT_ASSERT(o && o->type == ObjType_Foreign);
  ForeignData* d = (ForeignData*)obj_payload(o);
  if (d->flags & ForeignDead) return;
  d->flags |= ForeignDead;
  const ForeignType* type = heap_foreign_type(h, d->typeId);
  if (type && type->finalize && h->vm) type->finalize(h->vm, foreign_payload(d));
}

void heap_finalize_foreign_objects(Heap* h) {
  for (u32 i = 0; i < h->finalizable.len; i++)
    if (vec_at(&h->finalizable, i)->type == ObjType_Foreign)
      heap_finalize_foreign(h, vec_at(&h->finalizable, i));
}

// ---------- helper constructors ----------

Value make_pair_h(Heap* h, Value car, Value cdr) {
  // The alloc below may collect and move car/cdr, so root them in the heap's
  // internal tempRoots across the allocation and read them back after.
  vec_push(&h->tempRoots, car);
  vec_push(&h->tempRoots, cdr);
  Obj* o = heap_alloc(h, ObjType_Pair, sizeof(PairData));
  PairData* d = (PairData*)obj_payload(o);
  d->cdr = vec_pop(&h->tempRoots);
  d->car = vec_pop(&h->tempRoots);
  return obj_v(Tag_Pair, o);
}

static u32 string_payload_size(u32 len) {
  const u32 overhead = (u32)sizeof(StringData) + 1u;
  if (len > UINT32_MAX - overhead) ot_fatal("string: size overflow");
  return overhead + len;
}

Value make_string_h(Heap* h, const char* bytes, u32 len) {
  Obj* o = heap_alloc(h, ObjType_String, string_payload_size(len));
  StringData* d = (StringData*)obj_payload(o);
  d->len = len;
  char* dst = (char*)obj_payload(o) + sizeof(StringData);
  memcpy(dst, bytes, len);
  dst[len] = 0;
  d->nchars = utf8_count(dst, len);
  return obj_v(Tag_String, o);
}

Value make_string_from_h(Heap* h, Value src, u32 byteOff, u32 len) {
  // Copy bytes out of a heap string. The alloc may move `src`, so root it in
  // tempRoots and re-derive the source pointer after — passing string_bytes(src)
  // into make_string_h directly is a use-after-free under a moving collect.
  u32 payloadSize = string_payload_size(len);
  vec_push(&h->tempRoots, src);
  Obj* o = heap_alloc(h, ObjType_String, payloadSize);
  src = vec_pop(&h->tempRoots);
  StringData* d = (StringData*)obj_payload(o);
  d->len = len;
  char* dst = (char*)obj_payload(o) + sizeof(StringData);
  memcpy(dst, string_bytes(src) + byteOff, len);
  dst[len] = 0;
  d->nchars = utf8_count(dst, len);
  return obj_v(Tag_String, o);
}

Value make_array_h(Heap* h, u32 cap) {
  Obj* o = heap_alloc(h, ObjType_Array, sizeof(ArrayData));
  ArrayData* d = (ArrayData*)obj_payload(o);
  d->len = 0;
  d->cap = cap;
  d->items = cap ? (Value*)ot_alloc((size_t)cap * sizeof(Value)) : nullptr;
  if (cap && !d->items) ot_fatal("array: out of memory");
  return obj_v(Tag_Array, o);
}

Value make_table_h(Heap* h) {
  Obj* o = heap_alloc(h, ObjType_Table, sizeof(TableData));
  TableData* d = (TableData*)obj_payload(o);
  d->count = 0;
  d->tombstones = 0;
  d->entries = nullptr;
  d->entriesLen = 0;
  d->entriesCap = 0;
  d->index = nullptr;
  d->indexCap = 0;
  d->indexWidth = 0;
  return obj_v(Tag_Table, o);
}

Value make_buffer_h(Heap* h) {
  Obj* o = heap_alloc(h, ObjType_Buffer, sizeof(BufferData));
  BufferData* d = (BufferData*)obj_payload(o);
  memset(&d->buf, 0, sizeof(Buf));  // zero-init; heap sweeps it via finalizable
  return obj_v(Tag_Buffer, o);
}

void array_reserve(Value arr, u32 n) {
  ArrayData* d = as_array(arr);
  if (n <= d->cap) return;
  u32 ncap = grow_capacity(d->cap, n, "array: capacity overflow");
  Value* ni = (Value*)ot_realloc(d->items, (size_t)ncap * sizeof(Value));
  if (!ni) ot_fatal("array: out of memory");
  d->items = ni;
  d->cap = ncap;
}

// Access State's leading Heap without introducing a state.c link dependency in
// substrate tests. State::heap must remain its first data member.
static Heap* heap_of(State* vm) { return (Heap*)vm; }

Value make_pair(State* vm, Value car, Value cdr) { return make_pair_h(heap_of(vm), car, cdr); }
Value make_string(State* vm, const char* b, u32 n) { return make_string_h(heap_of(vm), b, n); }
Value make_string_buf(State* vm, const Buf* b) {
  return make_string(vm, b->data ? b->data : "", b->len);
}
Value make_string_from(State* vm, Value src, u32 off, u32 n) {
  return make_string_from_h(heap_of(vm), src, off, n);
}
Value make_array(State* vm, u32 cap) { return make_array_h(heap_of(vm), cap); }
Value make_table(State* vm) { return make_table_h(heap_of(vm)); }
Value make_buffer(State* vm) { return make_buffer_h(heap_of(vm)); }
