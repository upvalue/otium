# Otium C++ runtime — shared interface contract (v0)

Binding contract for parallel implementation work. Every file under `src/`
must compile against exactly these signatures. Internals are free; the
boundary is not. C++20, `-fno-exceptions -fno-rtti`, headers allowed in
`src/`: `cstdint cstring cstddef cmath cstdarg`. No STL containers, no
iostream under `src/`. Everything lives in `namespace ot`.

## common.hpp

```cpp
namespace ot {
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t; using u64=uint64_t;
using i8=int8_t;  using i16=int16_t;  using i32=int32_t;  using i64=int64_t;
using f64=double;
void ot_fatal(const char* msg);           // abort with message (host hook later)
#define OT_ASSERT(cond) /* fatal on failure in debug; no-op in release */
}
```

## vec.hpp

```cpp
template <typename T> struct Vec {   // malloc-backed growable vector
  T* data; u32 len; u32 cap;
  Vec(); ~Vec();                     // no copy; move ok
  void push(T v); T pop(); T& operator[](u32 i); void clear();
  void reserve(u32 n);
};
struct Buf {                          // byte/string builder
  char* data; u32 len; u32 cap;
  Buf(); ~Buf();
  void push(char c); void append(const char* s, u32 n); void appendCstr(const char* s);
  void printf(const char* fmt, ...);  // snprintf-based
  void clear();
};
```

## value.hpp

```cpp
enum class Tag : u8 {
  Nil, Null,        // Null = the empty list ()
  False, True,
  Int, Float,
  Symbol, Keyword,  // payload: u32 intern id (immediates, not heap)
  String, Pair, Array, Table, Buffer, Function, Macro, Param, Restart,
  Unwind,           // internal sentinel: an unwind is in flight
};

struct Obj;                      // heap object header, defined in heap.hpp
struct Value {                   // 16 bytes
  Tag tag;
  union { i64 i; f64 f; u32 id; Obj* obj; };
};

// constructors (all inline, in value.hpp)
Value nil_v(); Value null_v(); Value bool_v(bool b);
Value int_v(i64 i); Value float_v(f64 f);
Value symbol_v(u32 id); Value keyword_v(u32 id);
Value obj_v(Tag t, Obj* o);      // for all heap tags
Value unwind_v();

// accessors / tests (inline)
bool is_nil(Value), is_falsy(Value), is_truthy(Value), is_unwind(Value);
bool is_heap(Value v);           // tag >= String && tag <= Restart
bool val_eq(Value a, Value b);        // eq? semantics (identity/immediate)
bool val_equal(Vm& vm, Value a, Value b); // equal? semantics (deep) — defined in builtins

// Every function that can raise returns Value; Tag::Unwind means "unwinding,
// propagate now". The macro:
#define OT_TRY(expr) { Value _v = (expr); if (_v.tag == Tag::Unwind) return _v; }
```

Ints are `i64`, wrap two's-complement (do arithmetic in `u64`, cast back).

## heap.hpp / heap.cpp

```cpp
enum class ObjType : u8 { String, Pair, Array, Table, Buffer, Function, Macro, Param, Restart };
struct Obj {
  ObjType type; u8 flags; u16 _pad; u32 size;   // size = payload bytes
  Obj* forward;                                  // forwarding ptr during scavenge
  u32 ident;                                     // lazy identity id, 0 = unstamped
};
// payload structs follow the header; accessors like:
struct PairData   { Value car, cdr; };
struct StringData { u32 len; u32 nchars; /* utf8 bytes follow */ };
struct ArrayData  { Value* items; u32 len; u32 cap; };            // items in C heap, owned
struct TableData  { /* compact dict internals free */ u32 count; };
struct FunctionData; // see eval section
struct BufferData { Buf buf; };
struct ParamData  { u32 name; Value defaultVal; };
struct RestartData{ u32 name; Value description; u64 restartId; };

struct Heap {
  explicit Heap(struct Vm* vm, u32 initialBytes);
  Obj* alloc(ObjType t, u32 payloadBytes);   // may collect; roots = vm stack + namespaces
  void collect();
  u32 identityOf(Obj* o);                    // stamp lazily, stable across GC
};
```

Helper constructors (in heap.hpp or value.hpp): `Value make_pair(Vm&, Value, Value)`,
`Value make_string(Vm&, const char* bytes, u32 len)`,
`Value make_string_from(Vm&, Value src, u32 byteOff, u32 len)` (substring copy that roots
`src` across the alloc — required whenever the source bytes live on the GC heap),
`Value make_array(Vm&, u32 cap)`, `Value make_table(Vm&)`, `Value make_buffer(Vm&)`.
The Value-taking constructors root their arguments internally (Heap::tempRoots).
Accessors: `PairData* as_pair(Value)`, etc.

Table API (implemented in builtins/data.cpp but declared in heap.hpp):
```cpp
Value table_get(Vm&, Value table, Value key);        // nil on miss
Value table_put(Vm&, Value table, Value key, Value v); // nil value deletes; returns table
void  table_iter(Vm&, Value table, /* callback or index-based: */ u32 entryCount(Value),
                 bool entryAt(Value table, u32 i, Value* k, Value* v)); // live entries in order
Value array_get(Value arr, i64 idx);                 // nil out of range
void  array_push(Vm&, Value arr, Value v);
```
CONTRACT: table_get/table_put/array_get/array_push/array_reserve never allocate on the
GC heap (C-heap storage only); raw Values may be held across them. Everything else that
constructs values can collect and move every heap object.

## intern.hpp

```cpp
struct Intern {
  u32 intern(const char* s, u32 len);   // idempotent
  const char* name(u32 id, u32* lenOut);
};
```

## vm.hpp

```cpp
struct VmConfig { u32 heapBytes; u32 stackSlots; u32 maxDepth; };
using WriteFn  = void (*)(void* ud, const char* s, u32 n);
using LoadFn   = bool (*)(void* ud, const char* nsName, Buf* srcOut); // require callback

struct Vm {
  Heap heap; Intern intern;
  Vec<Value> stack;                 // GC root: the value stack
  // namespace registry, current ns, gensym counter — internals free
  volatile bool interruptFlag;
  WriteFn writeFn; void* writeUd;
  LoadFn loadFn;  void* loadUd;
  // unwind state: what is unwinding (condition value / restart transfer / quit)
  Value unwindCondition;            // valid when an Unwind is in flight

  static Vm* create(const VmConfig&);   // evaluates embedded prelude into otium.core
  void destroy();

  // native calling convention: args at stack[base..base+argc)
  u32 push(Value v);                // returns slot index (rooting)
  void popTo(u32 base);
};
using NativeFn = Value (*)(Vm& vm, u32 base, u32 argc);

// GC rooting discipline (lan-6mpt): internal code keeps heap values in rooted
// vm.stack slots and reads them at point of use; a raw Value local must never
// live across an allocating call. The handle types (vm.hpp):
struct Slot  { Value get() const; void set(Value) const; };  // names a stack cell
struct Scope {                       // RAII push/popTo balance; early-return safe
  explicit Scope(Vm&);               // base = current stack top; ~Scope pops to it
  Slot push(Value v = nil_v());
  Slot slot(u32 i);                  // i-th push of this scope
};
// Natives read arguments via ARG(n) (builtins.hpp) — an lvalue into vm.stack —
// at point of use, never via a copy held across an allocating call. Returning
// a Value is safe when the caller immediately roots or returns it.

// raising from native code:
Value raise_error(Vm&, const char* fmt, ...);  // builds {:type 'error :message ...}, returns Unwind
Value signal_value(Vm&, Value condition, bool unwindIfUnhandled);
```

## reader.hpp

```cpp
struct Reader {
  Reader(Vm& vm, const char* src, u32 len, const char* filename);
  // reads one form; out param set; returns: 1 = got form, 0 = eof, Unwind path:
  Value next();     // a form, or nil_v() with atEof()==true, or Unwind on read error
  bool atEof() const;
};
```
Positions: reader records line/col for pairs in an identity-keyed side table
owned by the Vm (`vm.setPos(Obj*, line, col)` — stub ok initially).

## printer.hpp

```cpp
void print_repr(Vm&, Value, Buf& out);
void print_display(Vm&, Value, Buf& out);
```

## eval.hpp

```cpp
Value eval_form(Vm&, Value form);            // expand + evaluate one top-level form
Value apply(Vm&, Value callee, u32 base, u32 argc); // args on stack
struct FunctionData {
  u32 name;              // intern id or 0
  Value params;          // the parameter list form
  Value body;            // list of body forms
  Value env;             // captured lexical env (impl free; nil for natives)
  Value nsName;          // defining namespace (symbol)
  NativeFn native;       // non-null for natives
  Value docstring;
};
```

## ns.hpp

```cpp
struct VarData;  // GC cell: value + metadata (name, ns, docstring, private flag)
Value ns_resolve(Vm&, Value symbol);        // full 3.1 resolution; Unwind if unresolvable
Value ns_define(Vm&, u32 name, Value v, bool isPrivate, Value docstring);
void  ns_switch(Vm&, u32 nsName);
```

## builtins

Each `builtins/*.cpp` exposes `void register_arith(Vm&); void register_data(Vm&);
void register_string(Vm&); void register_sys(Vm&);` — each defines its natives
into `otium.core` via `ns_define`.

## meson layout

```
meson.build              # lib 'otium' from src/**, exe 'otium' from repl/main.cpp,
                         # test exe 'otium-tests' from tests/*.cpp + doctest (vendored
                         # single header at tests/doctest.h), option 'gc_stress'
src/{common,vec,value,heap,intern,vm,ns,eval,reader,printer}.{hpp,cpp}
src/builtins/{arith,data,string,sys}.cpp
prelude/{expander,prelude}.scm
tools/embed.py           # .scm -> C array; custom_target generates prelude_embedded.h
repl/main.cpp
tests/test_*.cpp         # doctest
tests/otium/*.scm + run-tests.py + expected/
```

Tests include `"doctest.h"` and one `tests/main.cpp` defines
`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`. Doctest runs with exceptions ON in the
test binary (only `src/` is -fno-exceptions).
