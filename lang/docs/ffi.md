# Portable foreign function interface

Status: draft for review, October 16, 2026. These APIs are proposed, not
implemented. Names and release boundaries below are concrete design choices.

Otium should load a native shared library and call its C ABI from the same
binding source on Chez and C. Binding an ordinary exported function must not
require writing or compiling a C wrapper. SDL3 is the first acceptance target:
windows, accelerated 2D rendering, textures, input events, and queued PCM audio.

The design starts with Otium's two runtimes and the operations needed to call
SDL. Chez already has native foreign calls; C needs a calling engine, proposed
here as libffi, plus the platform dynamic loader. Both expose a small common
interface. The existing `ot_nat` extension API remains an embedding interface.

## Design basis

The shared contract covers function signatures, native memory, and conversions
at the call boundary. It does not require a general binding framework, custom
type translators, a declaration macro language, or identical internal value
representations. A binding should mostly be ordinary Otium definitions of
foreign procedures, followed by ordinary Otium code using them.

The necessary facilities follow from the acceptance program:

| SDL operation | FFI requirement |
| --- | --- |
| Create a window or renderer | Load a library, bind a function, pass scalars and strings, return a pointer. |
| Draw rectangles and upload pixels | Allocate native memory, write typed fields/elements, pass an address. |
| Poll input | Allocate a correctly sized event union and read fields at verified offsets. |
| Read timestamps and combine flags | Preserve native integer bits without rounding. |
| Queue audio | Pass a sample buffer and a null callback. |
| Recover from failure and exit | Copy error strings and explicitly release native resources. |

Runtime primitives provide loading, calls, allocation, and typed memory access.
Named fields and aggregate layouts can be implemented as library data and
helpers over those primitives. SDL constants and layouts belong to the SDL
binding. Resource wrappers, platform library search policy, and event conversion
belong to ordinary library code; they do not enlarge the runtime FFI contract.

The API below is a concrete proposal for those operations. Common Lisp CFFI
is prior art establishing feasibility, not an API or compatibility target.

## Release boundary

Version 1 supports fixed-arity C calls, scalar arguments and results, opaque
pointers, foreign allocations, pointers to structs and unions, fixed arrays,
UTF-8 strings, and output parameters. Native 64-bit types are supported with
checked conversion to each runtime's integer range. Declarations and memory
operations share a common contract, with the numeric range difference described
below; Chez's other native conversion extensions are not exposed.

Initial supported targets are macOS arm64 and Linux x86-64, using the native
C calling convention, 64-bit pointers, and the normal platform ABI. A target
is supported only after passing the ABI fixture suite. Windows, 32-bit targets,
mobile startup, and cross-compilation are later work.

Callbacks into Otium, varargs, structs/unions passed or returned by value,
packed structs, bitfields, flexible array members, long double, and C++ ABI
calls are excluded. Declarations requesting these features fail at binding
time. Function-pointer slots may contain null; there is no callable callback
constructor in version 1. SDL APIs requiring callbacks are outside this release.

Queued audio is within scope: `SDL_OpenAudioDeviceStream` accepts a null
callback, after which the application supplies or retrieves samples explicitly.
The initial audio example exercises playback.
[SDL audio stream contract](https://wiki.libsdl.org/SDL3/SDL_OpenAudioDeviceStream)

## Common source and loading

The portable API lives in `otium.ffi`. Examples below assume it is available as
`ffi`; runtime-specific launchers may establish that alias. The shared binding
body uses parentheses, quoted symbols, ordinary lists, and `#f`. It does not
depend on C's keywords, arrays, tables, or nil. Unifying `require` syntax and
file suffixes is separate work; a thin loader adapter is allowed per runtime.

The core operations are ordinary procedures. Scalar type specifications are
symbols; aggregate specifications are first-class layout values. Type lists
containing layout values are built with ordinary list operations. A declaration
macro may later provide sugar, but is not required for this release.

```scheme
(define sdl (ffi/open-library '("libSDL3.dylib")))
(define init
  (ffi/bind sdl "SDL_Init" '(u32) 'bool))
(define create-window
  (ffi/bind sdl "SDL_CreateWindow" '(utf8 int int u64) 'pointer))
(define poll-event
  (ffi/bind sdl "SDL_PollEvent" '(pointer) 'bool))
(define last-error
  (ffi/bind sdl "SDL_GetError" '() 'utf8))
```

`open-library` takes a nonempty list of path/name strings, tries them in order,
and returns an opaque library object. Paths containing directory components
are used directly; bare names use the OS loader's search rules. Otium adds no
implicit current-directory search. Failure reports all attempted candidates
and loader diagnostics. Library loading executes native initialization code.

The SDL package resolves candidates in this order: an explicit `SDL3_LIBRARY`
path, otherwise the installation manifest's path, otherwise platform names
(`libSDL3.dylib` on macOS; `libSDL3.so.0`, then `libSDL3.so` on Linux). An
explicit override that fails is an error, not permission to try another SDL.

`bind` takes a library, exact exported symbol name, argument-type list, and
result type. It resolves eagerly and returns an ordinary callable procedure.
Missing symbols, invalid types, and unsupported signatures fail immediately.
Wrong arity or argument conversion fails before native code runs.
`(ffi/has-symbol? library name)` is the explicit optional-symbol probe.

Resolution is scoped to the selected library handle and its loader-defined
dependencies. Chez must resolve that address explicitly and construct the
foreign procedure from it; global name lookup alone is insufficient when two
libraries export the same name. A small shared loader helper is allowed.

Loaded libraries remain resident for the process lifetime. There is no unload
operation in version 1. Reopening the same canonical library reuses its handle.
Bindings retain their library and immutable signature. Rebinding a name creates
a new callable; previously saved callables retain their old signature/address.
Reload does not destroy SDL resources or rerun SDL initialization implicitly.

## Types and conversion

| Specification | Native representation and Otium contract |
| --- | --- |
| `void` | Result only; returns `#f`. |
| `bool` | C `_Bool`; accepts only `#t`/`#f`, returns a boolean. |
| `i8 u8 i16 u16 i32 u32 i64 u64` | Exact-width signed/unsigned integers. |
| `short ushort int uint long ulong size-t ptrdiff-t` | Target C types; sizes, alignment, and signedness come from the runtime's target ABI. |
| `float double` | C floating types; accept Otium integers or inexact real numbers. |
| `pointer` | Opaque data pointer, including the explicit null pointer. |
| `callback-pointer` | Native function pointer; accepts only `ffi/null` in version 1. |
| `utf8` | Call argument string or null pointer; result is a copied string or null pointer. |
| `(array T count)` | Fixed-size foreign memory array; not a call argument/result by value. |
| Layout value | Immutable struct/union description for memory access; calls pass its address as `pointer`. |

Integer conversion checks signedness and bounds; no truncation, wrapping,
boolean coercion, or conversion from a float is permitted. Float inputs are
rounded to the target precision. Finite inputs that overflow that precision
raise a range error; NaN and infinities are accepted and preserved by class.
Float results are Otium inexact numbers. IEEE binary32/binary64 are required
for the initial targets.

Plain C `char` is not a version 1 type. Use `i8` or `u8` according to the
foreign API's contract. Runtime scalar definitions do not depend on SDL or
any library manifest; a manifest can verify them but cannot redefine them.

The backend must verify `_Bool` width and calling behavior with the native
fixture rather than assuming Chez's `boolean` foreign type matches C `_Bool`.
`callback-pointer` is represented according to the target function-pointer ABI;
the initial target profile requires that the backend can pass a null callback.

Version 1 uses each runtime's existing ordinary integers. The `i64` and `u64`
signatures describe native storage and calling conventions, not a promise that
all their values fit an Otium integer. Native results and typed memory reads
outside the receiving runtime's integer range raise `range`. Inputs and memory
writes must fit the declared native type. A failed write leaves memory unchanged;
a result-conversion error occurs after the native call and does not undo it.

For the initial 64-bit C target, the ordinary integer range is `OT_INT_MIN`
through `OT_INT_MAX`, currently `[-2^62, 2^62-1]`. Chez retains its wider exact
integers. Shared code must account for this numeric range difference; conversion
rules agree wherever both runtimes can represent the value. Integers are never
silently rounded to doubles, truncated, or wrapped by the FFI.

There is no separate foreign-integer type or FFI arithmetic API. The FFI copies
numeric payloads into native call storage and never passes the address of a
managed integer object. Full-width or arbitrary-precision language integers
can later widen the accepted range without changing binding signatures. They
are not a prerequisite for this release. Tests must still exercise values
outside C's range, even when ordinary SDL workloads do not reach them.

## Strings at the call boundary

The proposed spelling `utf8` means a NUL-terminated UTF-8 C string conversion,
not a distinct Otium string type. It is not a length-delimited byte buffer.
An input accepts an Otium string or `ffi/null`. A string supplies encoded bytes
with a trailing zero, valid throughout the native call. Embedded NUL is an
error before the call. The bytes are read-only to the callee; use explicit
foreign memory for writable strings. The callee must not retain the address.

This is a lifetime guarantee, not a requirement to use a particular allocator.
The C backend initially copies into native storage outside the moving heap.
Chez uses its native `utf-8` conversion where it satisfies the validation rules
below. That conversion creates a managed temporary bytevector; Chez's `string`
foreign type is an alias for `utf-8`. Chez bindings do not use `__collect_safe`
in version 1. There are no Otium threads or callbacks in this release, and
pinning is not part of the public API.
[Chez string conversion and collection rules](https://cisco.github.io/ChezScheme/csug10.0/foreign.html)

A `utf8` result is copied and decoded into an independent Otium string before
returning to application code. Native null becomes `ffi/null`, distinct from
the empty string. Invalid UTF-8 raises a conversion error on both backends;
native decoding defaults must not weaken that contract. The conversion never
frees the source pointer. The binding author must ensure the source remains
valid during conversion and contains a terminating NUL.
Unlike `read-utf8`, this conversion has no byte bound: the declared native
result contract supplies a valid terminated string but no length. Use a
`pointer` result and bounded `read-utf8` when a maximum extent is available
or that termination contract cannot be assumed.

APIs retaining an input string are declared with `pointer` and receive memory
from `ffi/alloc-utf8`. Its owner must remain reachable until native use ends,
then the application calls `ffi/free!`. A returned string requiring a native
destructor likewise uses a `pointer` result: copy it with `ffi/read-utf8`, then
call the required destructor under cleanup protection. Binary and explicitly
length-delimited data use a pointer and length, including embedded zero bytes.

## Pointers and foreign memory

`ffi/null` is the dedicated null pointer value on both runtimes. It is a pointer
value, distinct from integer zero, `#f`, C nil, and any empty string. It is
truthy; `(ffi/null? value)` is the test, returning false for every other value.
Integers, `#f`, and C nil are not accepted in its place. No change to language
truthiness is required.

Null inputs become the native null pointer for the declared argument type.
Null pointer results and pointer-valued memory reads become `ffi/null`.
This also applies to `utf8` results; a void result remains `#f` and a false
boolean remains `#f`. A null callback uses the target's function-pointer ABI.
All non-null data pointers are opaque pointer objects, not numeric addresses.
`ffi/pointer=?` accepts pointer values, including `ffi/null`, and compares
addresses; it does not imply equal ownership or valid lifetime. There is no
public integer-to-pointer conversion in version 1.

Chez's native conventions stay inside the adapter: raw `void*` uses integer
addresses, typed foreign pointers wrap an address, and string conversion maps
null to `#f`. The adapter normalizes these to and from Otium's dedicated null.
[Chez pointer representations](https://cisco.github.io/ChezScheme/csug10.0/foreign.html)

| Operation | Contract |
| --- | --- |
| `(ffi/alloc T count)` | Zeroed native storage for `count` elements, correctly aligned; returns an owning pointer. |
| `(ffi/alloc-utf8 string)` | Owning NUL-terminated UTF-8 allocation; rejects embedded NUL; released with `free!`. |
| `(ffi/free! p)` | Releases an allocation from `alloc`; invalidates it and all derived views. Repeated release of that allocation is a no-op. |
| `(ffi/sizeof T)` / `(ffi/alignof T)` | Size/alignment in bytes. |
| `(ffi/ref p T index)` | Reads one scalar or pointer at `index * sizeof(T)`. |
| `(ffi/set! p T index value)` | Writes one scalar or pointer; returns `#f`. |
| `(ffi/view p byte-offset byte-length)` | Non-owning byte span retaining its original allocation owner. |
| `(ffi/field-ref p T path)` | Reads a scalar/pointer field; `path` is a list of field symbols and array indices. |
| `(ffi/field-set! p T path value)` | Writes a scalar/pointer field; returns `#f`. |
| `(ffi/field-view p T path)` | Returns a view of an aggregate, array, or field, retaining the containing allocation. |
| `(ffi/offset-of T path)` | Returns a field's byte offset. |
| `(ffi/copy! dst src bytes)` | Overlap-safe native memory copy; returns `#f`. |
| `(ffi/read-utf8 p max-bytes)` | Copies through the first NUL within the bound; errors if none exists. |

Counts/indices are nonnegative exact integers. Arithmetic overflow is an error.
Zero-length allocation returns a distinct releasable allocation with no readable
bytes. `void` and `utf8` are not memory element types; C strings use `u8` storage.
Aggregate reads/writes use fields or views, not implicit language records.

Pointers and views carry no element type. `alloc` uses T only to determine
allocation size and alignment; each access supplies its interpretation through
T. `view` records a byte extent, not a layout. Its extent must fit within a
known parent span; for an external pointer with unknown extent, the caller
asserts that the requested span is valid. Nested views cannot widen their parent.
Each typed access checks its own alignment and fits entirely within the span.
`field-view` uses the supplied layout to compute the field's offset and size,
then returns that same kind of byte span. Reinterpreting bytes with another T
is allowed if those checks pass; the FFI does not enforce C aliasing rules.

Owned-memory operations check live state, bounds, and alignment. Views cannot
be freed independently. Null dereferences fail before access. Native allocations
require explicit `free!`, normally under `unwind-protect`; collection does not
release them. The current C extension mechanism finalizes on explicit release
or state destruction, not on collection. Version 1 does not add collection-time
finalizers. Keeping a view alive keeps its ownership metadata alive. `free!`
rejects pointers returned by SDL and other allocators.

Each runtime instance tracks outstanding FFI allocations for leak reporting and
reclaims them at instance teardown. Losing every pointer without calling `free!`
is still a leak during that instance's lifetime. Tests inspect outstanding
allocation counts/bytes before teardown so teardown cleanup cannot hide leaks.

The pointer wrapper and ownership metadata may move during collection; their
native allocation does not. Views retain shared ownership metadata so an
explicit release invalidates every view without depending on wrapper addresses.
`free!` also accepts allocations from `alloc-utf8`; freeing `ffi/null` is a no-op.

Pointers returned by foreign functions have unknown extent and no inferred
owner. Access through them is an unsafe operation governed by the foreign API.
Otium checks null and alignment but cannot prove the address is live or large
enough. Pointer fields loaded from memory likewise do not inherit ownership of
their pointee. Storing a pointer in native memory does not root its Otium owner;
the caller must retain that owner for the entire native use.

The FFI cannot prevent a C function from overrunning a buffer or retaining a
temporary pointer. Declaring a signature and managing foreign lifetimes is
trusted program behavior, not a sandbox boundary.

## Layout declarations and header information

```scheme
(define SDL_FRect
  (ffi/define-struct! 'SDL_FRect
    '((x float) (y float) (w float) (h float))))
(define rectangle (ffi/alloc SDL_FRect 1))
(ffi/field-set! rectangle SDL_FRect '(w) 100.0)

(define TwoRects
  (ffi/define-struct! 'TwoRects
    (list (list 'rects (list 'array SDL_FRect 2)))))
```

`define-struct!` and `define-union!` accept a name and ordered list of
`(field-name type)` entries and return a fresh immutable layout value. Despite
their names, these procedures do not install bindings or mutate a type registry.
The name is a diagnostic symbol only. `define` binds the returned value; normal
lexical and namespace scoping handles local names and imports. Procedures such
as `alloc`, `sizeof`, and `field-ref` consume the layout value directly and never
look up a caller namespace. Quoting an aggregate variable instead of passing
its value is an invalid type specification.

A type specification T is a built-in scalar symbol, a layout value, or an
`(array T count)` list containing another type specification and a nonnegative
exact count. Array/aggregate types are for memory only. Nested layouts are
passed as values when constructing the field list, as in `TwoRects` above.
Constructors validate and copy the specification, including array descriptions;
later mutation of the supplied lists does not change the layout. Duplicate
fields, invalid types, and recursive by-value layouts are errors. Opaque handles
use `pointer`, so they do not require recursive aggregate declarations.

Reevaluating a declaration constructs a new layout and rebinds the variable.
Saved layout values and enclosing layouts retain their original descriptions.
There is no restart requirement, implicit layout migration, or global replacement
of existing layouts. Pointers and allocations remain untyped byte spans: they
do not retain the layout used at allocation. Rebinding a layout cannot resize
old storage or protect it from interpretation using the wrong new layout. Code
must retain the old layout alongside old storage, or release and recreate that
storage when its layout changes. Bounds/alignment checks do not prove that two
layouts have compatible field meanings.

Struct fields follow target C alignment with tail padding to aggregate alignment.
Union members start at offset zero; union size is the largest member rounded up
to union alignment. Arrays have contiguous elements with the element's `sizeof`
as stride. The programmer selects the active union member; the FFI cannot infer
it. Enum storage uses an explicit ABI-verified integer type.

Layout declarations alone do not discover C preprocessor constants, typedefs,
or library ABI changes. The SDL package ships a versioned manifest generated
from pinned SDL headers using a small compiled C probe. It records header
version/hash, target triple, compiler ABI options, scalar sizes/alignments,
typedef representations, constants, and `sizeof`, `_Alignof`, and `offsetof`
for every aggregate and field used by the package.

Setup selects or generates the matching manifest; startup needs neither a C
compiler nor header files. Unsupported target manifests fail clearly. The
shared declaration layer compares calculated layouts with the manifest before
allowing any SDL call using them. CI regenerates and compares the manifest
against the actual headers. Cross-compilation must not run a host probe and
label its output as a target ABI.

Layout computation is the general path for libraries without a manifest.
For SDL, the header probe is the external ABI authority and the computation
is checked against it. A mismatch is an `abi` error; offsets are never silently
overridden. Thus there is one layout used for access, with an independent check
against the library's headers rather than two competing runtime layouts.

`SDL_Event` must have its full native size/alignment, including union members
and padding even when only some event variants have public accessors. Copying
only the known fields is insufficient for `SDL_PollEvent` or `SDL_PushEvent`.
[SDL event definition](https://wiki.libsdl.org/SDL3/SDL_Event)

The initial header baseline is SDL 3.4.12, matching the existing pinned Chez
binding in `chez/sdl/DEPENDENCY.txt`. The package checks `SDL_GetVersion` before
initialization and requires SDL major 3 and version >= 3.4.12. It relies on SDL's
compatible runtime ABI within that major version; the header probe does not
prove the internals of an arbitrary installed binary. A required missing symbol
is an error even if the version check passes. SDL2 is a separate binding package.

## Native calls and conditions

Calls run synchronously on the calling Otium thread. They are not cancellable
in flight. An interrupt is delivered after the native call and conversion
cleanup complete. The FFI does not promise that a blocking native call leaves
the REPL responsive.

Each backend keeps call inputs, callable metadata, and allocation owners live
through conversion and the native call. The C backend roots managed values
across every allocation and reacquires managed payload addresses after any
operation that may collect. Its native argument storage, copied strings, and
result storage are outside the moving heap; it passes no managed object address
to C in version 1. This is the C backend strategy, not a restriction on Chez's
internal implementation of the same lifetime contract.

Chez bindings do not use `__collect_safe` in version 1. Native code must not
reenter Otium during a call. Blocking calls therefore block the Otium execution
thread, and the version 1 contract does not support an embedding host enabling
concurrent collection during those calls. Threads or callbacks would require
revisiting this contract and the lifetime of automatic string temporaries.

Allocation failure or conversion failure releases all call temporaries. Once
native code has run, a result-conversion error does not undo its side effects.

FFI errors use Otium's existing condition protocol with type `ffi-error` and
structured data identifying operation, library/symbol where applicable, argument
index, expected type, and reason. Stable reasons are `load`, `symbol`, `signature`,
`arity`, `type`, `range`, `encoding`, `null`, `bounds`, `alignment`, `released`,
`allocation`, and `abi`. Messages may include native loader details.

Portable code reads these conditions through accessors, without depending on
tables, keywords, or native condition records:

| Operation | Result |
| --- | --- |
| `(ffi/error? value)` | Boolean identifying an FFI condition; accepts any value. |
| `(ffi/error-reason condition)` | Ordinary symbol from the reason list above. |
| `(ffi/error-operation condition)` | Ordinary symbol naming the FFI operation, such as `bind` or `ref`. |
| `(ffi/error-library condition)` | Selected library path/name string, or `#f` when unavailable. |
| `(ffi/error-symbol condition)` | Native symbol name string, or `#f` when inapplicable. |
| `(ffi/error-argument condition)` | Zero-based argument index for the failing operation or bound native call, or `#f`. |
| `(ffi/error-expected-type condition)` | Expected FFI type specification, or `#f`. |

Accessors other than `error?` require an FFI condition and raise a `type` error
otherwise. `condition-message` supplies human-readable text. Field accessors
return the same kinds of values on both runtimes; their internal condition
representation is not part of this API. A load error has reason `load`, library
`#f` if no candidate was selected, and a message listing attempted candidates.

Foreign return values are returned literally according to their type. The FFI
does not interpret null, false, negative integers, errno, or SDL error strings
as failures. SDL wrappers check each function's documented success convention,
then copy `SDL_GetError` immediately and raise an Otium condition if required.
`SDL_PollEvent` returning false simply means no event.
[SDL error contract](https://wiki.libsdl.org/SDL3/SDL_GetError)

## SDL package acceptance surface

The package contains shared raw declarations and thin Otium wrappers. No
SDL-specific C call wrappers are required for this surface. A setup-time ABI
probe and the generic runtime backend are allowed native components.

| Area | Required bindings and behavior |
| --- | --- |
| Startup | `SDL_GetVersion`, `SDL_SetMainReady`, `SDL_Init`, `SDL_Quit`, `SDL_GetError`. |
| Window | `SDL_CreateWindow`, `SDL_DestroyWindow`, `SDL_SetWindowTitle`. |
| Renderer | `SDL_CreateRenderer`, `SDL_DestroyRenderer`, `SDL_GetRendererName`, `SDL_SetRenderVSync`, `SDL_SetRenderLogicalPresentation`. |
| Drawing | `SDL_SetRenderDrawColor`, `SDL_RenderClear`, `SDL_RenderLine`, `SDL_RenderFillRect`, `SDL_RenderDebugText`, `SDL_RenderPresent`. |
| Textures | `SDL_CreateTexture`, `SDL_UpdateTexture`, `SDL_RenderTexture`, `SDL_DestroyTexture`; upload a known pixel buffer with explicit pitch. |
| Readback | `SDL_RenderReadPixels`, `SDL_SaveBMP`, `SDL_DestroySurface`. |
| Events | `SDL_PollEvent`, `SDL_PushEvent`; quit, window close/resize, keyboard, mouse motion/button/wheel, text input. |
| Text input | `SDL_StartTextInput`, `SDL_StopTextInput`; copy event text before the next event pump. |
| Time | `SDL_GetTicksNS`, `SDL_Delay`; exact timestamp transport. |
| Audio | `SDL_OpenAudioDeviceStream`, `SDL_PutAudioStreamData`, `SDL_GetAudioStreamQueued`, `SDL_ResumeAudioStreamDevice`, `SDL_DestroyAudioStream`; null callback and userdata. |

Bindings also expose the constants, typedefs, `SDL_Rect`, `SDL_FRect`,
`SDL_AudioSpec`, and event layouts needed by those functions. The manifest,
not handwritten numeric literals, supplies enums, flags, and audio formats.
Text events contain a pointer to UTF-8 data; copying event bytes alone does
not preserve the text. The event wrapper returns a tagged ordinary list with
copied payloads, not a view into the reusable event buffer. Unknown event types
return a type/timestamp-only record and never inspect an unknown union member.
[SDL text input layout](https://wiki.libsdl.org/SDL3/SDL_TextInputEvent)

The desktop host owns the main thread and calls `SDL_SetMainReady` before SDL
initialization when bypassing SDL's entry point. Video, polling, rendering, and
graphics teardown occur on that thread. This is not a portable substitute for
platform-specific application startup on mobile or other excluded targets.
[SDL startup](https://wiki.libsdl.org/SDL3/SDL_SetMainReady),
[SDL event thread requirements](https://wiki.libsdl.org/SDL3/SDL_PollEvent)

The audio example allocates an explicit PCM buffer, fills it through typed
memory writes, queues it, and resumes playback. It periodically checks queued
bytes to avoid unbounded buffering. `SDL_PutAudioStreamData` copies its input,
so that buffer may be reused once the call returns.
[SDL audio data ownership](https://wiki.libsdl.org/SDL3/SDL_PutAudioStreamData)

The acceptance program may use raw pointers and explicit SDL destruction calls.
A resource wrapper layer is optional and has no bearing on whether the FFI is
complete. If added, it should track closed state and dependencies: textures
must be destroyed before their renderer, and renderers before their window.
Such wrappers cannot protect independently retained raw pointer aliases.

Graphics resources have no GC destructor that calls SDL on an arbitrary thread.
The application uses `unwind-protect` to release partially created resources
and close everything before `SDL_Quit`. The acceptance host tracks its resources
and closes them in dependency order. No pointer or resource may cross an Otium
runtime instance boundary. The initial host runs one SDL session per process;
reloading drawing code preserves that session. This is a host policy, not a
new session abstraction required of the FFI.

## Backend work and repository placement

The C runtime gets an optional `WITH_FFI=1` build using libffi. A build without
it has no libffi link dependency and reports FFI unavailable on import. Core
startup does not load SDL. SDL remains an optional installed dependency on both
runtimes.

The C callable representation needs per-binding context: native address,
signature, library reference, and prepared libffi call interface. The existing
`ot_nat(ots*, otv*, int)` callback does not carry that context directly. Add a
dedicated foreign-callable object or context-bearing native callable; do not
generate a fixed pool of global C trampolines. Cache immutable prepared
signatures and keep all per-call storage separate.
[libffi call interface](https://github.com/libffi/libffi/blob/master/doc/libffi.texi)

Chez constructs and caches native procedures for the same signatures, with
the common validation/conversion layer around them. Its native integer coercion
rules must not leak through the portable API. Any dynamic compilation happens
at bind time, not on every call.
[Chez foreign interface](https://www.scheme.com/csug8/foreign.html)

Proposed placement: `c/src/ot-ffi.c` for the C backend,
`chez/otium/ffi.sls` for Chez, `shared/ffi/` for declarations, generic native
helpers, probes and fixtures, and `shared/sdl/` for binding data and wrappers.
Each runtime's loader may adapt the shared source without duplicating function
signatures. Integration tests belong under `shared/tests/`. Language/library
gaps discovered while porting scene code belong in `GAPS.txt`.

## Verification and implementation order

1. Build a tiny independent native fixture library and implement load/bind,
   scalar conversion, null pointers, and fixed-arity calls on both backends.
   Test native and runtime integer boundaries, including high-bit u64 results
   that succeed on Chez and raise `range` on C. Check failed writes leave memory
   unchanged and out-of-range results do not hide native side effects. Test bool
   ABI, float rounding, missing symbols, and two libraries exporting the same
   symbol with different results.
2. Implement allocations, derived views, structs/unions/arrays and the ABI probe.
   Verify sizes, alignment, nested offsets, pointer-to-pointer output parameters,
   zero initialization, bounds, double release, and use after release. Verify
   UTF-8, invalid encoding, embedded NUL rejection, empty versus null strings,
   and result-copy lifetime. Verify dedicated null round trips through calls
   and pointer fields; zero and `#f` must not be accepted as pointer arguments.
3. Run the same fixture cases under C GC stress and forced compaction, with
   allocations during conversions and live derived views. On Chez, test the
   automatic string path with collections between calls and verify that bound
   calls omit `__collect_safe`. Use an explicitly retained native string across calls
   and collections on both runtimes. Exercise failure cleanup and resource counts.
   Run `make gc-lint` when LLVM tooling is installed
   for changes involving allocation, roots, or managed pointers.
   Check leak counts before teardown, including an intentionally abandoned
   allocation to verify that collection does not silently release it. Exercise
   FFI error accessors from shared code, byte-view bounds, and layout/manifest
   mismatch errors. Define same-labelled layouts in separate namespaces, import
   them as values, and rebind one with changed fields. Verify saved and nested
   layouts keep their original size/offsets, without changing old allocations.
4. Bind SDL and port the current canvas feature set. Both runtimes must create
   a window, report an accelerated renderer, render and capture known pixels,
   upload a texture, process injected R/Escape/close/mouse events, and close
   cleanly after an injected Otium error. Validate text copying with a fixture
   whose backing bytes change after the next poll.
5. Add queued audio playback with a null callback, bounded queue refill, and
   deterministic teardown. Dummy audio can verify queue mechanics in CI; a
   real-device smoke test establishes actual playback. Keep those results distinct.
6. Exercise reload between frames while resource identity persists. The existing
   Chez SDL demos stay available until the shared path passes on both runtimes.

The fixture suite runs without SDL or a display and checks expected values,
not merely agreement between runtimes. SDL graphics tests require a real
accelerated backend and compare captured output; absence is an explicit skip
in ordinary CI and a failure in the dedicated integration job. Each supported
target must run the ABI and native-call fixtures on both runtimes. Report
foreign allocation leaks separately from managed heap use.

The release is complete when one shared SDL binding body and one portable
render/input/audio acceptance program run on both runtimes, with only loader
and host setup adapters. No per-function C glue is needed for the listed surface.

## Later callback work

Callbacks require a separate extension specifying registration lifetime,
procedure roots, reentry, thread attachment, exception containment, and shutdown
ordering. No Otium exception may unwind through an arbitrary native frame.
SDL audio callbacks may run away from the Otium thread; adding a native queue
bridge is a different contract from directly invoking Lisp. Do not register an
ordinary Otium procedure as a callback until those rules and tests exist.
