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

## GC and rooting

The collector is a moving semispace scavenger: every live object relocates when
it runs, so anything you hold across an allocation is stale. The value stack is
the root set. `Ref` names a slot in it, `OT_SCOPE(vm)` opens a region and
unwinds it on every exit path, and `ref_get`/`ref_set` go through the slot so
you always see the current address. Collections hold their backing storage in
separate GC objects, which is why `array_push`, `array_reserve`, `table_put` and
`buffer_append` allocate and can move the collection itself.

So: take `Ref` for parameters that can carry a heap value, keep a raw `Value`
only between a `ref_get` and its immediate use, and re-derive interior pointers
(`ArrayData*`, `TableEntry*`, string bytes, bytecode) after anything that can
allocate. `src/state.h` and `src/heap.h` hold the details, including `Status`
for functions that return a heap value.

Breaking the rule is fine for immediates and for stretches you have checked
cannot allocate. Say so at the site, with what would have to change for it to
stop holding.

`OT_GC_STRESS` collects on every allocation. It is the only thing that reliably
catches a missed root, and it is slow enough to be worth it only when you are in
the collector, the VM core, or the rooting machinery itself -- not for routine
work. `OT_GC_STRESS_EVERY=N` throttles it.
