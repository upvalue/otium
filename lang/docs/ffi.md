# FFI

The contract is [specs/ffi.md](../specs/ffi.md). This page covers what's
implemented. Shared layout and API code is in `lib/otium/ffi.ot`.

```sh
./otium examples/math.ot
./otium --runtime=c examples/math.ot
just test ffi
```

`examples/math.ot` calls `cos`, `hypot` and `modf` from the system math library
and prints `1.0`, `5.0`, `0.75` and `12.0`. `modf` exercises an output parameter
through an explicit foreign allocation.

The C runtime needs libffi, found with pkg-config; without it, importing
`otium.ffi` reports FFI unavailable. Chez uses its own foreign procedures, with
addresses supplied by the small `libotium-ffi` helper. `chez/run` points
`OTIUM_FFI_LIBRARY` at the helper in the build directory unless it's already set.

Implemented: scalar calls, null pointers, checked integer and float conversions,
UTF-8 copying, allocations, byte views, scalar and field memory access,
immutable struct and union layouts, embedded arrays, and structs and unions by
value as arguments and results. Aggregate results are owning allocations and
need `free!`. Bindings keep their address and prepared signature. By-value
lowering is described in [native components](native.md#by-value-aggregates).

The fixture in `tests/ffi/` has 197 checks on each runtime. It covers exact
values, library-scoped symbol lookup, failed-write preservation, integer range
differences, string lifetimes across collections, allocation counts, released
views, and unions across register classes, nesting, register exhaustion and
indirect returns.

Callbacks are implemented on Chez only, with `foreign-callable` and
`lock-object`; the contract is in [specs/ffi.md](../specs/ffi.md#callbacks).
`tests/ffi/callbacks.ot` has 40 Chez checks: repeated, stored and nested
callbacks, a collection inside a callback while a string argument is live,
contained errors and escapes, reload through a forwarding procedure, rejected
signatures and callback counts. Separate runs check that an exit inside a
callback happens after the call and that a callback from another thread aborts.
On C, `ffi/callback` fails as unsupported.

Not done yet: structured error metadata on every failure path, allocation-failure
injection and signature-cache sharing in C, callbacks on C, and varargs. Only macOS
arm64 with Chez 10.4.1 has been tested. Linux x86-64 must pass the same fixtures
before it's claimed. The Chez aggregate path needs a Chez with `(& ftype)`.
