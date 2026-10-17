# FFI implementation status

The first target is the system math library. The same Otium program calls
`cos`, `hypot`, and `modf` on C and Chez. `modf` exercises an output parameter
using an explicit foreign allocation. No SDL installation or display is needed.

From `lang/`:

```sh
make ffi
tools/python examples/run_math.py --runtime c
tools/python examples/run_math.py --runtime chez
make test-ffi
```

Both examples print `1.0`, `5.0`, `0.75`, and `12.0`. On macOS the launcher
loads libSystem; on Linux it tries `libm.so.6`, then `libm.so`. The launcher only
establishes the namespace alias and library candidates. The binding declarations
and program are in `examples/math.ot`.

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
layout and API code lives in `shared/ffi/core.ot`.

The native fixture checks exact expected values, library-scoped symbol lookup,
failed-write preservation, integer range differences, copied string lifetime,
retained strings across collections, allocation counts, and released views.
By-value tests cover a two-double struct, a mixed integer/double struct, and a
larger struct with an embedded array. Union cases cover mixed register classes,
homogeneous floating aggregates, nested unions and arrays, register exhaustion,
indirect returns, and saved bindings after layout rebinding. The fixture suite
has 197 checks on each runtime. C stress runs force collections and,
with the generational collector, compaction.

This is an implementation milestone, not completion of `specs/ffi.md`. Remaining
work includes complete structured error metadata for every failure path,
allocation-failure injection and signature-cache sharing in C. There is no callback or varargs
support. [Aggregate ABI notes](../shared/ffi/ABI.md) describe the union lowering
and its target-specific validation limits.

Validation so far is on macOS arm64 with Chez 10.4.1. Linux x86-64 remains
unverified and must pass the same ABI fixtures before being claimed supported.
The Chez aggregate path uses `(& ftype)` and needs a Chez version with that
facility; unsupported runtime versions have not yet been characterized.

## Portable SDL

SDL now uses one shared binding and canvas implementation in `shared/sdl/`.
There are no SDL-specific native call wrappers. The setup probe compiles against
installed SDL3 headers and records their version and SHA-256, target, compiler
options, constants, typedef representations, and aggregate sizes/alignments and
field offsets. The shared declarations verify those facts before using SDL.
The runtime minimum is SDL 3.4.12; the checked-in macOS arm64 manifest was
generated from SDL 3.4.16 headers. Regenerate locally when the installation
changes. Launching from an existing manifest needs no compiler or headers.

```sh
make ffi
make sdl-setup
tools/python shared/sdl/setup.py --check
tools/python examples/run_sdl.py --runtime c --scene shapes
tools/python examples/run_sdl.py --runtime chez --scene particles
make test-sdl
make test-sdl-integration
```

`test-sdl-integration` requires an accelerated renderer. Ordinary SDL testing
reports an explicit skip when accelerated graphics are unavailable. Dummy audio
checks queue mechanics; `--audio real` is a separate device smoke test. See
[SDL usage](../shared/sdl/USAGE.txt) for the acceptance and capture options.

The orbit and particle scenes now share source between runtimes. The host
watches scene files and reloads drawing code between frames, retaining the
window, renderer, phase, and frame count. R retries a reload; Escape and close
exit. Syntax and drawing errors can be fixed while the session stays open.
Resources are released explicitly on the SDL thread, in dependency order,
including after errors. The host reports outstanding foreign allocations
separately from managed heap use.

The C Raylib extension, automatic Raylib detection/linking, companion module,
examples, and smoke test were removed. The bespoke Chez canvas, SDL host,
chez-sdl3 download/wrapper setup, generated-binding dependency, and old SDL tests
were removed. Core runtime startup does not load SDL. The default C build still
has no libffi dependency; `WITH_FFI=1` enables it explicitly. The former Raylib
roguelike was retired, not ported; its font and render-target features are
outside this SDL acceptance surface. `GAPS.txt` records that remaining example
work. README files were intentionally left unchanged.

SDL validation on macOS arm64 with SDL 3.4.16 and Chez 10.4.1:

- The complete core C/Chez and shared REPL suites pass after removing the old
  integrations. A default C binary links only libSystem on this host.
- Both runtimes pass the SDL header/manifest checks, 15 headless event/audio
  fixture assertions, and 21 accelerated acceptance assertions on Metal.
  Acceptance covers known pixels, texture upload, injected input, preserved
  resources across drawing redefinition, and teardown after an Otium error.
- Both migrated scenes run on both runtimes. Watched reload tests wait for
  observed frames, recover from syntax and drawing errors, preserve resource
  identity and ticks, and verify the final BMP pixel. Renderer creation failure
  after window creation also leaves SDL uninitialized and no foreign allocations.
- Dummy audio and separate real-device smoke runs consume queued PCM and leave
  zero foreign allocations. Audible output has not been independently confirmed.
- The shared SDL integration passes with both C collectors; acceptance also
  passes with C GC stress. The 197 general FFI checks
  still pass on both runtimes, with C stress enabled.
- Focused GC lint passes for main.c and ot-ffi.c. Full `make gc-lint WITH_FFI=1`
  was killed while analyzing the existing otium.c; it is not a full lint pass.

Linux x86-64 ABI execution and SDL integration remain unverified. This migration
does not claim completion of the two-platform release contract in `ffi.md`.
