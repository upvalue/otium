# otium

Otium is an experimental programming language. A small Lisp intended to be
malleable and intended for running in low memory environments, but with some
big-language features and ideas. Most directly Scheme like in heritage but
takes some ideas from Clojure (namespaces) and Janet (practicality, tables).

Use `ticket --help` to interact with the ticketing system.

# C codebase

- Otium is intended to be embedded-friendly, so think about memory usage and
  avoid dynamic allocation unless it's necessary.

- Objects that belong to the language itself should be managed on the GC heap.

## Rooting: Ref, OT_SCOPE, Status

The collector moves objects, so a heap `Value` sitting in a C local goes stale
at the next allocating call. The rule: a heap value lives on the value stack and
nowhere else. A raw `Value` may exist only between a `ref_get` and its immediate
use, with no allocating call in between. See `src/state.h`.

In practice:

- Open a region with `OT_SCOPE(vm)`. It restores the stack on every exit path,
  so never write a manual pop. One per function -- a second shadows the first
  and `-Wshadow` rejects it, which is the signal to extract a function.
- Root with `Ref x = ref_push(vm, v)`, read with `ref_get(vm, x)`, overwrite
  with `ref_set(vm, x, v)`. In a loop, reuse one handle rather than pushing per
  iteration.
- Take `Ref` for any parameter that can carry a heap value. That is what stops a
  caller passing a transient like `car_(form)`, so prefer it to taking `Value`
  and rooting on entry.
- Never hoist an interior pointer (`ArrayData*`, `TableData*`, `StringData*`,
  `TableEntry*`, a string's `const char*`, a Code object's bytecode pointer)
  across anything that can allocate. Re-derive it from the rooted handle each
  time.
- `array_push`, `array_reserve`, `table_put` and `buffer_append` **allocate**.
  Backing storage is on the GC heap, so growth can collect and move both the
  collection and any pointer into it. Keep the collection in a handle and read
  it back at every call; a raw local goes stale the first time one of these
  grows. This is the single easiest mistake to make in this codebase.

Functions that can return a heap value should return `Status` and leave the
result on the stack: `Status_Ok` means exactly one value pushed, `Status_Unwind`
means the stack is back to entry depth. Propagate with `OT_CHECK`. Functions
that only ever return immediates (arithmetic, comparisons, predicates) keep
returning `Value` and need none of this.

### When it is fine to break the rule

Breaking it is normal in the places below. What is not optional is *saying so at
the site*, with what would have to change for it to stop holding -- a bare
"this is safe" comment is what let the previous convention rot.

- Immediates. Ints, floats, symbols, keywords, nil and booleans carry no heap
  pointer. `add_constant_imm` exists for exactly this and asserts it.
- A raw `Value` or interior pointer held across code you have checked cannot
  allocate. `state_push`, `vec_push` on a C-heap vector, and the `emit_*`
  bytecode writers are allocation-free today.
- Boundaries with code that has not been converted, where a `Value` is returned
  and the caller roots it immediately.

### Core work

Editing the collector, `heap.c`, or the stack machinery itself means working
below the abstraction, and there the rule cannot help you. Things to know:

- `heap_alloc` can collect on any call, including the first.
- `heap.c` roots through `Heap::tempRoots` rather than the value stack, because
  the `*_h` constructors take a bare `Heap` and have no `State` to push onto.
  That is the one place two rooting mechanisms still coexist, and it is why.
- Bytecode is stored inline in the `Code` object, so `vm_execute`'s cached
  `bytes`/`ip`/`end` die at any allocation. Every opcode that can allocate ends
  with `VM_RELOAD()`. Adding an allocating opcode without one is a live bug that
  only `OT_GC_STRESS` will catch.
- `OT_GC_STRESS` collects on every single allocation. Build with it and run the
  suite before claiming a rooting change is correct; `OT_GC_STRESS_EVERY=N`
  throttles it when the full rate is too slow. This is the gate that makes a
  hand audit trustworthy, and it is not optional for changes in this area.
