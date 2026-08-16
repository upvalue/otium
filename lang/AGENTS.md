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

## GC values

`src/slots.h` is how code touches language values. Put heap values in `Ref`
slots, open one `OT_SCOPE(vm)` per function, and use the `ot_*` operations. An
operation writes a heap result to a caller-owned `Ref`; scalars such as ints,
intern ids, and copied bytes can stay in C locals. Raw `Value` is for
immediates, nil-or-unwind control flow, and `ot_ret(vm, ref)` in return position.

The collector moves every live object. Only these files work on heap internals
directly:

- `src/heap.c`
- `src/vm.c`
- `src/slots.c`
- `src/collections.c`

Those files define `OT_HEAP_INTERNALS` before including `heap.h`. They may use
raw heap values and interior pointers, but must root anything held across an
allocation and re-derive pointers afterward. Keep the non-allocating stretch
clear at the call site.

Do not add another permitted file to get around the slot API. `heap.h` rejects
unpermitted includes, and `tests/check_hygiene.py` rejects heap layout access
outside the list. Focused low-level tests have explicit permits of their own.
