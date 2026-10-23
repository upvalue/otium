# Native components

C and C++ in `native/` are bridges that would be impractically difficult to
write in Otium: calling-convention adapters, compiler-derived ABI facts, opaque
native resources, and library interfaces Otium can't yet express safely.
Performance alone isn't a reason to move Otium code into C. If an Otium
algorithm is too slow, improve it or the runtime. Resource ownership isn't
enough either: ordinary acquire/use/release sequences can use the FFI and
`unwind-protect`, as the window icon code in `lib/otium/ui/gpu.ot` does.

Use existing libraries where they fit. An optional component names its
dependencies and fails clearly when it's enabled without them. Otium owns
application policy and reloadable behavior, and both runtimes use the same
native boundary.

## Building

`meson.build` builds everything. FFI and UI are Meson features that default to
`auto`, so they're built when their dependencies are installed. Force one with
`meson configure build -Dffi=enabled` or turn it off with `-Dui=disabled`.

The C runtime links the bridges statically. Chez loads the shared libraries
from the build directory: `chez/run` builds `libotium-line` and `libotium-dev`
on first use, and `OTIUM_FFI_LIBRARY` overrides the FFI helper's path.

The UI build writes `build/ui-config.ot` and `build/ui-test-config.ot`. These
are installation paths read as data, not application policy. They and the
embedded font directory name this checkout, so rebuild after moving it.

## Components

### line/line-edit.c

Bestline terminal and callback adapter. One process-wide editor; snapshots are
copied before reading. The idle callback services the dev server while input
waits. Header comments describe returned-string and callback lifetimes.

### ffi/native.c

Compiler ABI facts and dynamic-loader adaptation. Library handles stay alive for
the process lifetime, including across namespace reloads. Runtime adapters own
managed values; shared code never stores their pointers.

### dev/transport.c

POSIX socket and poll adapter with bounded framing queues. One server per
process, on the interpreter thread. Invalid UTF-8 and embedded NUL disconnect a
client before a payload crosses the native-string boundary. Evaluation,
responses, interruption, socket naming and reload live in `lib/otium/dev.ot`.
The private-directory helper only creates or checks a path it is given.

### utf8/utf8.c

One byte-span validator for foreign-string ingress, shared by the FFI and the
transport. Embedded NUL is valid Unicode; the transport applies its own no-NUL
rule. This isn't a home for language algorithms.

### ui/layout.cpp

Yoga's callback and struct interface, exposed as opaque nodes and scalar
queries. Measurement is replayed with answers supplied by Otium, so managed
pointers and interpreter callbacks never enter Yoga's callback. Query indices
belong to the current measurement pass. Free nodes after detaching children.

### ui/text.c and ui/renderer.c

SDL_ttf custom text engine and SDL GPU. All calls run on the SDL thread.
Renderers borrow windows; destroy renderers before windows and before
`SDL_Quit`. Event payload strings last until the next poll and Otium copies them
immediately. Text handles are borrowed cache entries (see `native/ui/text.h`).
Reload must keep live handles until explicit teardown, and must not unload the
library.

## Policy still to move into Otium

These files contain more than a bridge should. Keep tests of their observable
behavior when reducing them.

- `line-edit.c`: token scanning, completion selection, help rendering and editor
  preferences. Keep Bestline callbacks and terminal and signal handling.
- `ui/text.c`: font selection, cache replacement and ligature caret
  interpolation. Expose text and cluster lifetimes and scalar cluster data.
- `ui/renderer.c`: atlas packing, primitive batching, per-window event routing
  and key naming. Keep only the GPU and SDL operations that can't reasonably go
  through the FFI.
- `dev/transport.c`: framing queues and limits travel with POSIX polling. A
  byte-buffer boundary would let Otium own framing without making strings from
  unvalidated bytes. Preserve malformed-input and backpressure behavior.

## Dependencies

| Dependency | Version | Source | License |
| --- | --- | --- | --- |
| libffi | any recent | pkg-config `libffi` | MIT |
| SDL3 | 3.4.12 or later | pkg-config `sdl3` | zlib |
| SDL3_ttf | 3.2.2 or later | pkg-config `sdl3-ttf` | zlib |
| Yoga | 3.2.1 | `subprojects/yoga.wrap`, pinned by SHA-256 | MIT |
| glslc | any | shader compiler for SPIR-V | Apache 2.0 |
| Bestline | vendored | `native/vendor/bestline/` | BSD-2 |

Meson downloads Yoga on first configure and builds it from
`subprojects/packagefiles/yoga/meson.build` rather than its own CMake build. The
Metal shader source and the compiled SPIR-V are embedded with `#embed`.

The fonts in `native/ui/fonts/` define the UI defaults rather than installed
system fonts. Each has its unmodified SIL Open Font License 1.1. Retrieved
October 17, 2026 from Google Fonts:

| File | Source | SHA-256 |
| --- | --- | --- |
| `NotoSans.ttf` | [Noto Sans variable font](https://github.com/google/fonts/blob/main/ofl/notosans/NotoSans%5Bwdth,wght%5D.ttf) | `bfb7bb691513f12e734dc346c03a03f784912432d7e3fa8e56efcf906fe86b3d` |
| `JetBrainsMono.ttf` | [JetBrains Mono variable font](https://github.com/google/fonts/blob/main/ofl/jetbrainsmono/JetBrainsMono%5Bwght%5D.ttf) | `48715a42ec242c21e9f02692891e147d022299a52e48d5e413e1a942193ffeda` |

## By-value aggregates

Otium keeps memory layout separate from call ABI classification. Structs and
embedded arrays become libffi struct descriptors, each checked against the
shared layout's size, alignment and field offsets. Chez uses private struct,
union and array ftypes through `(& ftype)`.

libffi has no union descriptor, so the C backend builds a synthetic struct with
the union's size, alignment and register classes. It's only used to marshal
bytes; field access still uses the union layout and no member is chosen as the
active variant.

On arm64, a union whose scalar leaves all share one float type and fit one to
four elements is represented by those floating elements. Other unions use
integer elements of the union's alignment. This keeps homogeneous floating
aggregate classification, including a union of one float and a larger float
array. See the [AAPCS64 aggregate rules](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst)
and [libffi's arm64 code](https://github.com/libffi/libffi/blob/master/src/aarch64/ffi.c).

On x86-64, the classifier merges the integer and floating classes of every
member at its byte offset, and integer wins where members overlap. The
synthetic struct uses alignment-sized elements so libffi can merge them into
SysV eightbytes at the real enclosing offset. That matters because a
four-byte-aligned union can start halfway through an eightbyte inside a struct;
classifying the union alone would wrongly turn an SSE eightbyte into an integer
one. Unions over 16 bytes go in memory. See [libffi's SysV classification](https://github.com/libffi/libffi/blob/master/src/x86/ffi64.c).

Packed and empty aggregates, zero-length arrays, bitfields, vectors, complex
numbers, long double and custom alignment aren't supported. Members are
naturally aligned scalars with alignments of 1, 2, 4 or 8 bytes. The x86-64
lowering is implemented but hasn't run on Linux yet.
