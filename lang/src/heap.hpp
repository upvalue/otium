// heap.hpp — semispace Cheney scavenger and heap object layouts.
//
// Design notes (read me, integrators):
//
// * Collection model: a single active space; collect() mallocs a fresh
//   to-space (same size, or doubled when live > 50% after the copy, capped
//   at maxBytes), Cheney-copies live objects into it, then frees the old
//   space. Functionally identical to a classic semispace, but growth is
//   trivial and idle memory is not held.
//
// * Roots: the heap does NOT reach into Vm directly (vm.cpp is a separate
//   compilation unit owned by another agent, and tests run with vm == null).
//   Instead, root sources register a walker:
//
//       heap.addRoots(&walkFn, userData);
//
//   where walkFn is called during collect() and must invoke visit(slot) for
//   every Value* it wants kept alive / updated. The Vm MUST register its
//   value stack (and its namespace registry) this way in Vm::create:
//
//       static void walkStack(void* ud, ot::Heap::VisitFn visit, void* ctx) {
//         Vm* vm = (Vm*)ud;
//         for (u32 i = 0; i < vm->stack.len; i++) visit(ctx, &vm->stack.data[i]);
//       }
//       vm->heap.addRoots(walkStack, vm);
//
// * C-heap payloads: ArrayData::items and TableData::entries live in the C
//   heap (malloc), owned by the object; BufferData::buf owns its bytes too.
//   The heap keeps a list of such "finalizable" objects; after each scavenge
//   the list is swept — dead entries have their C-heap storage freed, live
//   entries are updated to the moved object.
//
// * FunctionData is defined here (not eval.hpp) because the scavenger needs
//   its layout to trace params/body/env/nsName/docstring.

#pragma once
#include "common.hpp"
#include "vec.hpp"
#include "value.hpp"

namespace ot {

struct Vm;  // opaque here; heap never dereferences it

enum class ObjType : u8 { String, Pair, Array, Table, Buffer, Function, Macro, Param, Restart };

struct Obj {
  ObjType type;
  u8 flags;
  u16 _pad;
  u32 size;      // size = payload bytes
  Obj* forward;  // forwarding ptr during scavenge
  u32 ident;     // lazy identity id, 0 = unstamped
};

// Payload structs follow the header (8-byte aligned).
struct PairData {
  Value car, cdr;
};
struct StringData {
  u32 len;
  u32 nchars; /* utf8 bytes follow */
};
struct ArrayData {
  Value* items;
  u32 len;
  u32 cap;
};  // items in C heap, owned
// Compact-dict table layout (canonical here so the scavenger can trace it).
// Insertion-ordered entry vector + open-addressed index array, both C-heap,
// owned by the object. Tombstones are marked key.tag == Tag::Unwind and are not
// traced.
struct TableEntry {
  u64 hash;
  Value key;
  Value val;
};
struct TableData {
  u32 count;       // live entries
  u32 tombstones;  // dead entries still in `entries`
  TableEntry* entries;
  u32 entriesLen;
  u32 entriesCap;  // insertion order
  u8* index;       // open-addressed slot array, scaled width; 0 = empty,
  u32 indexCap;    // else entryIndex+1. Power-of-two capacity.
  u32 indexWidth;  // bytes per slot: 1, 2, or 4
};
struct BufferData {
  Buf buf;
};
using NativeFn = Value (*)(Vm& vm, u32 base, u32 argc);
struct FunctionData {
  u32 name;         // intern id or 0
  Value params;     // the parameter list form
  Value body;       // list of body forms
  Value env;        // captured lexical env (nil for natives)
  Value nsName;     // defining namespace (symbol)
  NativeFn native;  // non-null for natives
  Value docstring;
};
struct ParamData {
  u32 name;
  Value defaultVal;
};
struct RestartData {
  u32 name;
  Value description;
  u64 restartId;
};

inline void* obj_payload(Obj* o) { return (void*)((char*)o + sizeof(Obj)); }

struct Heap {
  using VisitFn = void (*)(void* ctx, Value* slot);
  using RootWalkFn = void (*)(void* ud, VisitFn visit, void* ctx);

  explicit Heap(Vm* vm, u32 initialBytes);
  ~Heap();
  Heap(const Heap&) = delete;
  Heap& operator=(const Heap&) = delete;

  Obj* alloc(ObjType t, u32 payloadBytes);  // may collect
  void collect();
  u32 identityOf(Obj* o);  // stamp lazily, stable across GC

  // Register a root walker; called on every collect. Not deregisterable
  // (walkers live as long as the heap).
  void addRoots(RootWalkFn fn, void* ud);

  // --- internals ---
  Vm* vm;       // opaque back-pointer for the owner; unused by heap
  char* space;  // active space
  u32 spaceSize;
  u32 used;      // bump offset into space
  u32 maxBytes;  // growth cap (default 64 MiB)
  u32 nextIdent;
  u64 collections;  // stats: number of collects run

  struct RootEntry {
    RootWalkFn fn;
    void* ud;
  };
  Vec<RootEntry> rootWalkers;
  Vec<Value> tempRoots;   // internal rooting for make_* argument values
  Vec<Obj*> finalizable;  // objects owning C-heap storage (Array/Table/Buffer)

  // scavenge state (valid only during collect)
  char* toSpace;
  u32 toSize;
  u32 toUsed;

  Obj* copyObj(Obj* o);
  void visitSlot(Value* slot);
  void collectInto(u32 newSize);
};

// Helper constructors. Take Vm& per the contract, but only use vm.heap —
// declared here, defined in heap.cpp via an extern accessor the Vm provides.
// For substrate tests, the heap-taking variants are also provided.
Value make_pair_h(Heap& h, Value car, Value cdr);
Value make_string_h(Heap& h, const char* bytes, u32 len);
// Substring copy from a heap string; roots src across the alloc. Use this
// (never make_string with a string_bytes-derived pointer) when the source
// bytes live on the GC heap.
Value make_string_from_h(Heap& h, Value src, u32 byteOff, u32 len);
Value make_array_h(Heap& h, u32 cap);
Value make_table_h(Heap& h);
Value make_buffer_h(Heap& h);

Value make_pair(Vm& vm, Value car, Value cdr);
Value make_string(Vm& vm, const char* bytes, u32 len);
Value make_string_from(Vm& vm, Value src, u32 byteOff, u32 len);
Value make_array(Vm& vm, u32 cap);
Value make_table(Vm& vm);
Value make_buffer(Vm& vm);

// Accessors.
inline PairData* as_pair(Value v) { return (PairData*)obj_payload(v.obj); }
inline StringData* as_string(Value v) { return (StringData*)obj_payload(v.obj); }
inline const char* string_bytes(Value v) {
  return (const char*)((char*)obj_payload(v.obj) + sizeof(StringData));
}
inline ArrayData* as_array(Value v) { return (ArrayData*)obj_payload(v.obj); }
inline TableData* as_table(Value v) { return (TableData*)obj_payload(v.obj); }
inline BufferData* as_buffer(Value v) { return (BufferData*)obj_payload(v.obj); }
inline FunctionData* as_function(Value v) { return (FunctionData*)obj_payload(v.obj); }
inline ParamData* as_param(Value v) { return (ParamData*)obj_payload(v.obj); }
inline RestartData* as_restart(Value v) { return (RestartData*)obj_payload(v.obj); }

// Array item growth helper (items live in the C heap; realloc-based).
void array_reserve(Value arr, u32 n);

// Table API — implemented in builtins/data.cpp, declared here per contract.
//
// CONTRACT (alloc-free): table_get, table_put, array_get, array_push, and
// array_reserve never allocate on the GC heap — all their storage growth is
// C-heap malloc/realloc. Callers may hold raw Values across these calls.
// If any of them ever needs a GC allocation, every such caller must be
// re-audited (grep for "alloc-free" first).
Value table_get(Vm&, Value table, Value key);           // nil on miss
Value table_put(Vm&, Value table, Value key, Value v);  // nil value deletes; returns table
Value array_get(Value arr, i64 idx);                    // nil out of range
void array_push(Vm&, Value arr, Value v);

}  // namespace ot
