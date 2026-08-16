// Semispace Cheney scavenger and heap object layouts.
#pragma once
#include "common.h"
#include "vec.h"
#include "value.h"

typedef struct State State;  // opaque here; heap never dereferences it

typedef enum ObjType : u8 {
  ObjType_String,
  ObjType_Pair,
  ObjType_Array,
  ObjType_Table,
  ObjType_Buffer,
  ObjType_Code,
  ObjType_Function,
  ObjType_Macro,
  ObjType_Param,
  ObjType_Restart,
  ObjType_Foreign,
} ObjType;

struct Obj {
  ObjType type;
  u8 flags;
  u16 _pad;
  u32 size;      // size = payload bytes
  Obj* forward;  // forwarding ptr during scavenge
  u32 ident;     // lazy identity id, 0 = unstamped
};

#define OBJ_PAIR_KEY ((u8)(1u << 0))

// Payload structs follow the header (8-byte aligned).
typedef struct PairData {
  Value car, cdr;
} PairData;
typedef struct StringData {
  u32 len;
  u32 nchars; /* utf8 bytes follow */
} StringData;
typedef struct ArrayData {
  Value* items;
  u32 len;
  u32 cap;
} ArrayData;  // items in C heap, owned
// Compact-dict table layout (canonical here so the scavenger can trace it).
// Insertion-ordered entry vector + open-addressed index array, both C-heap,
// owned by the object. Tombstones are marked key.tag == Tag_Unwind and are not
// traced.
typedef struct TableEntry {
  u64 hash;
  Value key;
  Value val;
} TableEntry;
typedef struct TableData {
  u32 count;       // live entries
  u32 tombstones;  // dead entries still in `entries`
  TableEntry* entries;
  u32 entriesLen;
  u32 entriesCap;  // insertion order
  u8* index;       // open-addressed slot array, scaled width; 0 = empty,
  u32 indexCap;    // else entryIndex+1. Power-of-two capacity.
  u32 indexWidth;  // bytes per slot: 1, 2, or 4
} TableData;
typedef struct BufferData {
  Buf buf;
} BufferData;
typedef struct CodeData {
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
} CodeData;
typedef Value (*NativeFn)(State* vm, u32 base, u32 argc);
// The collector needs the complete layout to trace every Value field.
typedef struct FunctionData {
  u32 name;         // intern id or 0
  Value code;       // Code for compiled functions, nil for natives
  Value nsName;     // defining namespace (symbol)
  NativeFn native;  // non-null for natives
  Value docstring;
  u32 nupvals;  // boxed captures stored inline after this struct
} FunctionData;
typedef struct ParamData {
  u32 name;
  Value defaultVal;
} ParamData;
typedef struct RestartData {
  u32 name;
  Value description;
  u64 restartId;
} RestartData;

// Foreign payloads deliberately cannot contain Values: the collector moves
// them byte-for-byte and does not trace their contents. Inline payload bytes
// follow ForeignData; external payloads store one pointer in that space.
enum ForeignFlag : u32 {
  ForeignDead = 1u << 0,
  ForeignExternal = 1u << 1,
};
typedef struct ForeignData {
  u32 typeId;  // 1-based index into Heap foreignTypes
  u32 flags;
  u32 payloadSize;  // inline byte count; sizeof(void*) for external mode
  u32 _pad;
} ForeignData;
typedef void (*ForeignFinalizer)(State* vm, void* payload);
typedef struct ForeignType {
  u32 nameSym;
  ForeignFinalizer finalize;
} ForeignType;

static inline void* obj_payload(Obj* o) { return (void*)((char*)o + sizeof(Obj)); }

typedef void (*VisitFn)(void* ctx, Value* slot);
typedef void (*RootWalkFn)(void* ud, VisitFn visit, void* ctx);

typedef struct RootEntry {
  RootWalkFn fn;
  void* ud;
} RootEntry;
OT_VEC_TYPE(RootEntry, VecRootEntry);
OT_VEC_TYPE(Value, VecValue);
OT_VEC_TYPE(Obj*, VecObjPtr);
OT_VEC_TYPE(ForeignType, VecForeignType);

#define OT_HEAP_MAX_DEFAULT (64u * 1024 * 1024)

typedef struct Heap {
  State* vm;    // opaque back-pointer for the owner; unused by heap
  char* space;  // active space
  u32 spaceSize;
  u32 used;      // bump offset into space
  u32 maxBytes;  // growth cap (default 64 MiB)
  u32 nextIdent;
  u64 collections;  // stats: number of collects run

  VecRootEntry rootWalkers;
  VecValue tempRoots;     // internal rooting for make_* argument values
  VecObjPtr finalizable;  // objects owning C-heap storage or foreign resources
  VecForeignType foreignTypes;

  // scavenge state (valid only during collect)
  char* toSpace;
  u32 toSize;
  u32 toUsed;
} Heap;

void heap_init(Heap* h, State* vm, u32 initialBytes, u32 maxBytes);
void heap_deinit(Heap* h);
Obj* heap_alloc(Heap* h, ObjType t, u32 payloadBytes);  // may collect
void heap_collect(Heap* h);
u32 heap_identity_of(Heap* h, Obj* o);  // stamp lazily, stable across GC
u32 heap_add_foreign_type(Heap* h, u32 nameSym, ForeignFinalizer finalize);
const ForeignType* heap_foreign_type(const Heap* h, u32 typeId);
void heap_finalize_foreign(Heap* h, Obj* o);
void heap_finalize_foreign_objects(Heap* h);

// The heap does not scan State directly. Register a walker for every external
// root source; each walker must visit all of its Value slots on collection.
// Walkers live as long as the heap and cannot be deregistered.
void heap_add_roots(Heap* h, RootWalkFn fn, void* ud);

// Heap-taking constructors support focused tests built around a bare Heap.
Value make_pair_h(Heap* h, Value car, Value cdr);
Value make_string_h(Heap* h, const char* bytes, u32 len);
// Substring copy from a heap string; roots src across the alloc. Use this
// (never make_string with a string_bytes-derived pointer) when the source
// bytes live on the GC heap.
Value make_string_from_h(Heap* h, Value src, u32 byteOff, u32 len);
Value make_array_h(Heap* h, u32 cap);
Value make_table_h(Heap* h);
Value make_buffer_h(Heap* h);

Value make_pair(State* vm, Value car, Value cdr);
Value make_string(State* vm, const char* bytes, u32 len);
Value make_string_buf(State* vm, const Buf* bytes);
Value make_string_from(State* vm, Value src, u32 byteOff, u32 len);
Value make_array(State* vm, u32 cap);
Value make_table(State* vm);
Value make_buffer(State* vm);

// Accessors.
static inline PairData* as_pair(Value v) { return (PairData*)obj_payload(v.obj); }
static inline StringData* as_string(Value v) { return (StringData*)obj_payload(v.obj); }
static inline const char* string_data_bytes(const StringData* s) { return (const char*)(s + 1); }
static inline const char* string_bytes(Value v) { return string_data_bytes(as_string(v)); }
static inline ArrayData* as_array(Value v) { return (ArrayData*)obj_payload(v.obj); }
static inline TableData* as_table(Value v) { return (TableData*)obj_payload(v.obj); }
static inline BufferData* as_buffer(Value v) { return (BufferData*)obj_payload(v.obj); }
static inline CodeData* as_code(Value v) { return (CodeData*)obj_payload(v.obj); }
static inline FunctionData* as_function(Value v) { return (FunctionData*)obj_payload(v.obj); }
static inline Value* function_upvals(FunctionData* fn) { return (Value*)(fn + 1); }
static inline ParamData* as_param(Value v) { return (ParamData*)obj_payload(v.obj); }
static inline RestartData* as_restart(Value v) { return (RestartData*)obj_payload(v.obj); }
static inline ForeignData* as_foreign(Value v) { return (ForeignData*)obj_payload(v.obj); }
static inline bool foreign_dead(Value v) { return (as_foreign(v)->flags & ForeignDead) != 0; }

// Extension-facing API. Type ids are per-VM and must be retained by the
// registering extension. Finalizers must not allocate on the Otium heap: GC
// invokes them while a collection is in progress.
u32 register_foreign_type(State* vm, const char* name, ForeignFinalizer finalize);
Value make_foreign_inline(State* vm, u32 typeId, const void* payload, u32 payloadBytes);
Value make_foreign_pointer(State* vm, u32 typeId, void* payload);
// On success, writes the inline payload address or external pointer to out.
// On type/dead errors, raises a condition and returns Tag_Unwind.
Value foreign_check(State* vm, const char* who, Value value, u32 expectedType, void** out);
// Runs the registered finalizer once and marks the object dead.
Value foreign_release(State* vm, const char* who, Value value, u32 expectedType);

// Array item growth helper (items live in the C heap; realloc-based).
void array_reserve(Value arr, u32 n);

// Table API — implemented in builtins/data.c.
// table_get, table_put, array_get, array_push, and
// array_reserve never allocate on the GC heap — all their storage growth is
// C-heap alloc/realloc. Callers may hold raw Values across these calls.
// If any of them ever needs a GC allocation, every such caller must be
// re-audited (grep for "alloc-free" first).
Value table_get(State* vm, Value table, Value key);           // nil on miss
Value table_put(State* vm, Value table, Value key, Value v);  // nil value deletes; returns table
Value array_get(Value arr, i64 idx);                          // nil out of range
void array_push(State* vm, Value arr, Value v);
