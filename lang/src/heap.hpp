// Semispace Cheney scavenger and heap object layouts.

#pragma once
#include "common.hpp"
#include "vec.hpp"
#include "value.hpp"

namespace ot {

struct State;  // opaque here; heap never dereferences it

enum class ObjType : u8 {
  String,
  Pair,
  Array,
  Table,
  Buffer,
  Code,
  Function,
  Macro,
  Param,
  Restart
};

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
struct CodeData {
  u8* bytes;
  Value* consts;
  u32 len;
  u32 constCount;
  u32 nfixed;
  u32 hasRest;
  u32 nupvals;
  u32 nlocals;
  u32 maxStack;
  u32 name;
};
using NativeFn = Value (*)(State& vm, u32 base, u32 argc);
// The collector needs the complete layout to trace every Value field.
struct FunctionData {
  u32 name;         // intern id or 0
  Value params;     // the parameter list form
  Value body;       // list of body forms
  Value env;        // captured lexical env (nil for natives)
  Value code;       // Code for compiled functions, nil for tree/native functions
  Value nsName;     // defining namespace (symbol)
  NativeFn native;  // non-null for natives
  Value docstring;
  u32 nupvals;  // boxed captures stored inline after this struct
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

  explicit Heap(State* vm, u32 initialBytes);
  ~Heap();
  Heap(const Heap&) = delete;
  Heap& operator=(const Heap&) = delete;

  Obj* alloc(ObjType t, u32 payloadBytes);  // may collect
  void collect();
  u32 identityOf(Obj* o);  // stamp lazily, stable across GC

  // The heap does not scan State directly. Register a walker for every external
  // root source; each walker must visit all of its Value slots on collection.
  // Walkers live as long as the heap and cannot be deregistered.
  void addRoots(RootWalkFn fn, void* ud);

  // --- internals ---
  State* vm;    // opaque back-pointer for the owner; unused by heap
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
  Vec<Obj*> finalizable;  // objects owning C-heap storage (Array/Table/Buffer/Code)

  // scavenge state (valid only during collect)
  char* toSpace;
  u32 toSize;
  u32 toUsed;

  Obj* copyObj(Obj* o);
  void visitSlot(Value* slot);
  void collectInto(u32 newSize);
};

// Heap-taking constructors support substrate code without a complete State.
Value make_pair_h(Heap& h, Value car, Value cdr);
Value make_string_h(Heap& h, const char* bytes, u32 len);
// Substring copy from a heap string; roots src across the alloc. Use this
// (never make_string with a string_bytes-derived pointer) when the source
// bytes live on the GC heap.
Value make_string_from_h(Heap& h, Value src, u32 byteOff, u32 len);
Value make_array_h(Heap& h, u32 cap);
Value make_table_h(Heap& h);
Value make_buffer_h(Heap& h);

Value make_pair(State& vm, Value car, Value cdr);
Value make_string(State& vm, const char* bytes, u32 len);
Value make_string(State& vm, const Buf& bytes);
Value make_string_from(State& vm, Value src, u32 byteOff, u32 len);
Value make_array(State& vm, u32 cap);
Value make_table(State& vm);
Value make_buffer(State& vm);

// Accessors.
inline PairData* as_pair(Value v) { return (PairData*)obj_payload(v.obj); }
inline StringData* as_string(Value v) { return (StringData*)obj_payload(v.obj); }
inline const char* string_bytes(const StringData* s) { return (const char*)(s + 1); }
inline const char* string_bytes(Value v) { return string_bytes(as_string(v)); }
inline ArrayData* as_array(Value v) { return (ArrayData*)obj_payload(v.obj); }
inline TableData* as_table(Value v) { return (TableData*)obj_payload(v.obj); }
inline BufferData* as_buffer(Value v) { return (BufferData*)obj_payload(v.obj); }
inline CodeData* as_code(Value v) { return (CodeData*)obj_payload(v.obj); }
inline FunctionData* as_function(Value v) { return (FunctionData*)obj_payload(v.obj); }
inline Value* function_upvals(FunctionData* fn) { return (Value*)(fn + 1); }
inline ParamData* as_param(Value v) { return (ParamData*)obj_payload(v.obj); }
inline RestartData* as_restart(Value v) { return (RestartData*)obj_payload(v.obj); }

// Array item growth helper (items live in the C heap; realloc-based).
void array_reserve(Value arr, u32 n);

// Table API — implemented in builtins/data.cpp.
// table_get, table_put, array_get, array_push, and
// array_reserve never allocate on the GC heap — all their storage growth is
// C-heap malloc/realloc. Callers may hold raw Values across these calls.
// If any of them ever needs a GC allocation, every such caller must be
// re-audited (grep for "alloc-free" first).
Value table_get(State&, Value table, Value key);           // nil on miss
Value table_put(State&, Value table, Value key, Value v);  // nil value deletes; returns table
Value array_get(Value arr, i64 idx);                       // nil out of range
void array_push(State&, Value arr, Value v);

}  // namespace ot
