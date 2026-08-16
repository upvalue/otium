// heap.cpp — semispace Cheney scavenger. See heap.hpp for the design notes.
#include "heap.hpp"
#include <new>  // placement new for BufferData::buf

namespace ot {

static void* foreignPayload(ForeignData* d) {
  if (!(d->flags & ForeignExternal)) return d + 1;
  void* payload = nullptr;
  memcpy(&payload, d + 1, sizeof payload);
  return payload;
}

static u32 align8(u32 n) {
  if (n > UINT32_MAX - 7u) ot_fatal("heap: size overflow");
  return (n + 7u) & ~7u;
}

static u32 objTotalSize(u32 payloadBytes) {
  u32 payloadSize = align8(payloadBytes);
  if (payloadSize > UINT32_MAX - (u32)sizeof(Obj)) ot_fatal("heap: size overflow");
  return (u32)sizeof(Obj) + payloadSize;
}

static u32 objTotalSize(Obj* o) { return objTotalSize(o->size); }

Heap::Heap(Vm* vm_, u32 initialBytes)
    : vm(vm_), spaceSize(initialBytes < 1024 ? 1024 : align8(initialBytes)), used(0),
      maxBytes(64u * 1024 * 1024), nextIdent(1), collections(0), toSpace(nullptr), toSize(0),
      toUsed(0) {
  space = (char*)malloc(spaceSize);
  if (!space) ot_fatal("heap: cannot allocate initial space");
}

Heap::~Heap() {
  // Free C-heap storage owned by still-live finalizable objects.
  for (u32 i = 0; i < finalizable.len; i++) {
    Obj* o = finalizable.data[i];
    switch (o->type) {
      case ObjType::Array: free(((ArrayData*)obj_payload(o))->items); break;
      case ObjType::Table: {
        TableData* td = (TableData*)obj_payload(o);
        free(td->entries);
        free(td->index);
        break;
      }
      case ObjType::Buffer: ((BufferData*)obj_payload(o))->buf.~Buf(); break;
      case ObjType::Foreign: finalizeForeign(o); break;
      default: break;
    }
  }
  free(space);
}

void Heap::addRoots(RootWalkFn fn, void* ud) { rootWalkers.push(RootEntry{fn, ud}); }

Obj* Heap::alloc(ObjType t, u32 payloadBytes) {
  // Check the configured cap before alignment/header arithmetic can wrap.
  if (payloadBytes > maxBytes) ot_fatal("heap: allocation exceeds cap");
  u32 total = objTotalSize(payloadBytes);
  if (total > maxBytes) ot_fatal("heap: allocation exceeds cap");
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
    collect();
  }
#endif
  OT_ASSERT(used <= spaceSize);
  if (total > spaceSize - used) {
    collect();
    while (total > spaceSize - used) {
      if (spaceSize > maxBytes / 2) ot_fatal("heap: out of memory (cap reached)");
      collectInto(spaceSize * 2);
    }
  }
  Obj* o = (Obj*)(space + used);
  used += total;
  o->type = t;
  o->flags = 0;
  o->_pad = 0;
  o->size = payloadBytes;
  o->forward = nullptr;
  o->ident = 0;
  memset(obj_payload(o), 0, payloadBytes);
  if (t == ObjType::Array || t == ObjType::Table || t == ObjType::Buffer || t == ObjType::Foreign)
    finalizable.push(o);
  return o;
}

Obj* Heap::copyObj(Obj* o) {
  if (o->forward) return o->forward;
  u32 total = objTotalSize(o);
  OT_ASSERT(toUsed + total <= toSize);
  Obj* n = (Obj*)(toSpace + toUsed);
  toUsed += total;
  memcpy(n, o, total);
  n->forward = nullptr;
  o->forward = n;
  return n;
}

void Heap::visitSlot(Value* slot) {
  if (is_heap(*slot) && slot->obj) slot->obj = copyObj(slot->obj);
}

static void visitTrampoline(void* ctx, Value* slot) { ((Heap*)ctx)->visitSlot(slot); }

void Heap::collect() { collectInto(spaceSize); }

void Heap::collectInto(u32 newSize) {
  toSize = newSize;
  toSpace = (char*)malloc(toSize);
  if (!toSpace) ot_fatal("heap: cannot allocate to-space");
  toUsed = 0;

  // 1. Copy roots (registered walkers + internal temp roots).
  for (u32 i = 0; i < rootWalkers.len; i++)
    rootWalkers.data[i].fn(rootWalkers.data[i].ud, visitTrampoline, this);
  for (u32 i = 0; i < tempRoots.len; i++) visitSlot(&tempRoots.data[i]);

  // 2. Cheney scan: walk to-space, tracing each copied object's fields.
  u32 scan = 0;
  while (scan < toUsed) {
    Obj* o = (Obj*)(toSpace + scan);
    void* p = obj_payload(o);
    switch (o->type) {
      case ObjType::Pair: {
        PairData* d = (PairData*)p;
        visitSlot(&d->car);
        visitSlot(&d->cdr);
        break;
      }
      case ObjType::Array: {
        ArrayData* d = (ArrayData*)p;
        for (u32 i = 0; i < d->len; i++) visitSlot(&d->items[i]);
        break;
      }
      case ObjType::Table: {
        TableData* d = (TableData*)p;
        for (u32 i = 0; i < d->entriesLen; i++) {
          if (d->entries[i].key.tag == Tag::Unwind) continue;  // tombstone
          visitSlot(&d->entries[i].key);
          visitSlot(&d->entries[i].val);
        }
        break;
      }
      case ObjType::Function:
      case ObjType::Macro: {
        FunctionData* d = (FunctionData*)p;
        visitSlot(&d->params);
        visitSlot(&d->body);
        visitSlot(&d->env);
        visitSlot(&d->nsName);
        visitSlot(&d->docstring);
        break;
      }
      case ObjType::Param: visitSlot(&((ParamData*)p)->defaultVal); break;
      case ObjType::Restart: visitSlot(&((RestartData*)p)->description); break;
      case ObjType::String:
      case ObjType::Buffer:
      case ObjType::Foreign: break;
    }
    scan += objTotalSize(o);
  }

  // 3. Sweep finalizable list: free C-heap storage of dead objects, update
  //    survivors to their new addresses.
  u32 keep = 0;
  for (u32 i = 0; i < finalizable.len; i++) {
    Obj* o = finalizable.data[i];
    if (o->forward) {
      finalizable.data[keep++] = o->forward;
    } else {
      void* p = obj_payload(o);
      switch (o->type) {
        case ObjType::Array: free(((ArrayData*)p)->items); break;
        case ObjType::Table: {
          TableData* td = (TableData*)p;
          free(td->entries);
          free(td->index);
          break;
        }
        case ObjType::Buffer: ((BufferData*)p)->buf.~Buf(); break;
        case ObjType::Foreign: finalizeForeign(o); break;
        default: break;
      }
    }
  }
  finalizable.len = keep;

#ifdef OT_GC_STRESS
  // Poison from-space so any stale Value read across a collection fails
  // loudly instead of silently seeing the old payload.
  memset(space, 0xAB, spaceSize);
#endif
  free(space);
  space = toSpace;
  spaceSize = toSize;
  used = toUsed;
  toSpace = nullptr;
  toSize = 0;
  toUsed = 0;
  collections++;

  // Grow policy: if live > 50% after the copy, double next time via an
  // immediate re-collect into a bigger space (cheap: live set is small).
  if (used > spaceSize / 2 && spaceSize <= maxBytes / 2) collectInto(spaceSize * 2);
}

u32 Heap::identityOf(Obj* o) {
  if (o->ident == 0) o->ident = nextIdent++;
  return o->ident;
}

u32 Heap::addForeignType(u32 nameSym, ForeignFinalizer finalize) {
  for (u32 i = 0; i < foreignTypes.len; i++) {
    if (foreignTypes[i].nameSym != nameSym) continue;
    if (foreignTypes[i].finalize != finalize) ot_fatal("foreign type registered twice");
    return i + 1;
  }
  foreignTypes.push(ForeignType{nameSym, finalize});
  return foreignTypes.len;
}

const ForeignType* Heap::foreignType(u32 typeId) const {
  if (typeId == 0 || typeId > foreignTypes.len) return nullptr;
  return &foreignTypes.data[typeId - 1];
}

void Heap::finalizeForeign(Obj* o) {
  OT_ASSERT(o && o->type == ObjType::Foreign);
  ForeignData* d = (ForeignData*)obj_payload(o);
  if (d->flags & ForeignDead) return;
  d->flags |= ForeignDead;
  const ForeignType* type = foreignType(d->typeId);
  if (type && type->finalize && vm) type->finalize(*vm, foreignPayload(d));
}

void Heap::finalizeForeignObjects() {
  for (u32 i = 0; i < finalizable.len; i++)
    if (finalizable[i]->type == ObjType::Foreign) finalizeForeign(finalizable[i]);
}

// ---------- helper constructors ----------

Value make_pair_h(Heap& h, Value car, Value cdr) {
  // The alloc below may collect and move car/cdr, so root them in the heap's
  // internal tempRoots across the allocation and read them back after.
  h.tempRoots.push(car);
  h.tempRoots.push(cdr);
  Obj* o = h.alloc(ObjType::Pair, sizeof(PairData));
  PairData* d = (PairData*)obj_payload(o);
  d->cdr = h.tempRoots.pop();
  d->car = h.tempRoots.pop();
  return obj_v(Tag::Pair, o);
}

static u32 stringPayloadSize(u32 len) {
  constexpr u32 overhead = (u32)sizeof(StringData) + 1u;
  if (len > UINT32_MAX - overhead) ot_fatal("string: size overflow");
  return overhead + len;
}

Value make_string_h(Heap& h, const char* bytes, u32 len) {
  Obj* o = h.alloc(ObjType::String, stringPayloadSize(len));
  StringData* d = (StringData*)obj_payload(o);
  d->len = len;
  char* dst = (char*)obj_payload(o) + sizeof(StringData);
  memcpy(dst, bytes, len);
  dst[len] = 0;
  d->nchars = utf8_count(dst, len);
  return obj_v(Tag::String, o);
}

Value make_string_from_h(Heap& h, Value src, u32 byteOff, u32 len) {
  // Copy bytes out of a heap string. The alloc may move `src`, so root it in
  // tempRoots and re-derive the source pointer after — passing string_bytes(src)
  // into make_string_h directly is a use-after-free under a moving collect.
  u32 payloadSize = stringPayloadSize(len);
  h.tempRoots.push(src);
  Obj* o = h.alloc(ObjType::String, payloadSize);
  src = h.tempRoots.pop();
  StringData* d = (StringData*)obj_payload(o);
  d->len = len;
  char* dst = (char*)obj_payload(o) + sizeof(StringData);
  memcpy(dst, string_bytes(src) + byteOff, len);
  dst[len] = 0;
  d->nchars = utf8_count(dst, len);
  return obj_v(Tag::String, o);
}

Value make_array_h(Heap& h, u32 cap) {
  Obj* o = h.alloc(ObjType::Array, sizeof(ArrayData));
  ArrayData* d = (ArrayData*)obj_payload(o);
  d->len = 0;
  d->cap = cap;
  d->items = cap ? (Value*)malloc((size_t)cap * sizeof(Value)) : nullptr;
  if (cap && !d->items) ot_fatal("array: out of memory");
  return obj_v(Tag::Array, o);
}

Value make_table_h(Heap& h) {
  Obj* o = h.alloc(ObjType::Table, sizeof(TableData));
  TableData* d = (TableData*)obj_payload(o);
  d->count = 0;
  d->tombstones = 0;
  d->entries = nullptr;
  d->entriesLen = 0;
  d->entriesCap = 0;
  d->index = nullptr;
  d->indexCap = 0;
  d->indexWidth = 0;
  return obj_v(Tag::Table, o);
}

Value make_buffer_h(Heap& h) {
  Obj* o = h.alloc(ObjType::Buffer, sizeof(BufferData));
  BufferData* d = (BufferData*)obj_payload(o);
  new (&d->buf) Buf();  // placement-construct; heap sweeps it via finalizable
  return obj_v(Tag::Buffer, o);
}

void array_reserve(Value arr, u32 n) {
  ArrayData* d = as_array(arr);
  if (n <= d->cap) return;
  u32 ncap = d->cap ? d->cap : 8;
  while (ncap < n) {
    if (ncap > UINT32_MAX / 2) ot_fatal("array: capacity overflow");
    ncap *= 2;
  }
  Value* ni = (Value*)realloc(d->items, (size_t)ncap * sizeof(Value));
  if (!ni) ot_fatal("array: out of memory");
  d->items = ni;
  d->cap = ncap;
}

// Access Vm's leading Heap without introducing a vm.cpp link dependency in
// substrate tests. Vm::heap must remain its first data member.
static Heap& heap_of(Vm& vm) { return *reinterpret_cast<Heap*>(&vm); }

Value make_pair(Vm& vm, Value car, Value cdr) { return make_pair_h(heap_of(vm), car, cdr); }
Value make_string(Vm& vm, const char* b, u32 n) { return make_string_h(heap_of(vm), b, n); }
Value make_string(Vm& vm, const Buf& b) { return make_string(vm, b.data ? b.data : "", b.len); }
Value make_string_from(Vm& vm, Value src, u32 off, u32 n) {
  return make_string_from_h(heap_of(vm), src, off, n);
}
Value make_array(Vm& vm, u32 cap) { return make_array_h(heap_of(vm), cap); }
Value make_table(Vm& vm) { return make_table_h(heap_of(vm)); }
Value make_buffer(Vm& vm) { return make_buffer_h(heap_of(vm)); }

}  // namespace ot
