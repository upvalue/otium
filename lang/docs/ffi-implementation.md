# FFI implementation status

The first target is the system math library. The same Otium program calls
`cos`, `hypot`, and `modf` on C and Chez. `modf` exercises an output parameter
using an explicit foreign allocation. No SDL installation or display is needed.

From `lang/`:

```sh
make ffi
python3 examples/shared/run-math.py --runtime c
python3 examples/shared/run-math.py --runtime chez
make test-ffi
```

Both examples print `1.0`, `5.0`, `0.75`, and `12.0`. On macOS the launcher
loads libSystem; on Linux it tries `libm.so.6`, then `libm.so`. The launcher only
establishes the namespace alias and library candidates. The binding declarations
and program are in `examples/shared/math.scm`.

C needs libffi and pkg-config for `WITH_FFI=1`. The default build has no libffi
link dependency and reports FFI unavailable when imported. Chez uses its native
foreign procedures, with exact addresses supplied by a small shared loader
helper. Set `OTIUM_FFI_LIBRARY` to the built `libotium-ffi` helper when launching
Chez directly; the example and test launchers set it automatically.

The implemented surface includes scalar calls, dedicated null pointers, checked
integer/float conversions, UTF-8 copying, allocations, byte views, scalar and
field memory access, immutable struct/union layouts, embedded arrays, and
struct/union-by-value arguments and results. Aggregate results are owning allocations
and need `free!`. Bindings retain their address and prepared signature. Shared
layout and API code lives in `shared/ffi/core.scm`.

The native fixture checks exact expected values, library-scoped symbol lookup,
failed-write preservation, integer range differences, copied string lifetime,
retained strings across collections, allocation counts, and released views.
By-value tests cover a two-double struct, a mixed integer/double struct, and a
larger struct with an embedded array. Union cases cover mixed register classes,
homogeneous floating aggregates, nested unions and arrays, register exhaustion,
indirect returns, and saved bindings after layout rebinding. The fixture suite
has 197 checks on each runtime. C stress runs force collections and,
with the generational collector, compaction.

This is an implementation milestone, not completion of `docs/ffi.md`. Remaining
work includes complete structured error metadata for every failure path,
allocation-failure injection, signature-cache sharing in C, and the SDL
manifest, binding, and acceptance program. There is no callback or varargs
support. [Aggregate ABI notes](../shared/ffi/ABI.md) describe the union lowering
and its target-specific validation limits.

Validation so far is on macOS arm64 with Chez 10.4.1. Linux x86-64 remains
unverified and must pass the same ABI fixtures before being claimed supported.
The Chez aggregate path uses `(& ftype)` and needs a Chez version with that
facility; unsupported runtime versions have not yet been characterized.
