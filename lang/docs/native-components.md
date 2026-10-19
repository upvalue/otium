Native components
=================

C and C++ belong here only as bridges that would be impractically difficult to
write in Otium: calling-convention adapters, compiler-derived ABI facts, opaque
native resources, and library interfaces that Otium cannot yet express safely.
Performance alone is not a reason to move Otium code into C. Improve the Otium
implementation or its runtime/compiler when an Otium algorithm is too slow.
Resource ownership alone is not sufficient either: ordinary acquire/use/release
sequences can use FFI and unwind-protect, as the window icon code does.

Use existing libraries where they fit. Optional components may require optional
libraries such as SDL. The component must name its dependencies and fail clearly
when explicitly enabled without them. Do not build handwritten substitutes just
to avoid an optional dependency. Otium owns application policy and reloadable
behavior; both runtimes consume the same native boundary.

Build
-----

`shared/Makefile` owns compilation of shared native sources. Its optional
`shared/ui/build.mk` fragment adds UI targets. The C runtime consumes the compiled
objects and Chez loads shared libraries; neither runtime has separate recipes
for these sources. Base builds require Make and a C compiler. SDL, a C++ compiler,
pkg-config and shader tools are only needed when requesting UI targets.

Run from `lang/`:

```sh
make -C shared              # line editing, dev transport and UTF-8 check binary
make ffi                    # C runtime with libffi and the shared FFI helper
make ui-setup               # explicitly download and verify pinned Yoga sources
make ui                     # SDL3/SDL3_ttf/Yoga bridge and native UI test helpers
make -C shared test         # run native UTF-8 boundary checks
make test-native-policy     # Otium application policy tests
make clean                  # remove current-platform runtime/native build output
```

For a separate output directory or toolchain:

```sh
make -C shared BUILD=build/local CC=clang CFLAGS='-O0 -g' all ffi test
make -C shared BUILD=build/local CXX=clang++ CXXFLAGS='-O0 -g' ui
```

`BUILD` and `PLATFORM` select output paths. `CC`, `CXX`, `AR`, `CPPFLAGS`, `CFLAGS`,
`CXXFLAGS`, `LDFLAGS` and `LDLIBS` configure compilation and linking. UI also
accepts `PKG_CONFIG`, `GLSLC`, `YOGA_ROOT`, `SDL_LIBRARY` and `CONFIG_DIR` overrides.
Normal builds do not download dependencies. Pinned Yoga sources compile directly
with the C++ compiler; no generated build system is involved.

Shared libraries remain in `shared/build/<platform>/`. Base objects live in
`objects/`; UI objects, generated shaders and test executables live in `ui/`.
Compiler-generated dependency files track headers. Separate base/UI configuration
stamps update when flags change, including when switching back to earlier flags.
Building a launcher helper does not change the UI configuration.

The C runtime requests objects in its own
`c/build/<platform>/native/<flags-hash>/` directory so runtime compiler flags
cannot contaminate Chez's helper libraries. Its standalone archive includes the
FFI bridge and validator when FFI is enabled.

`make ui` writes `shared/build/ui-config.ot` and `ui-test-config.ot` by default.
These are installation paths, not application policy. `CONFIG_DIR` changes that
destination. They name this checkout's libraries; rebuild after moving it.
SDL headers/libraries are discovered through pkg-config.
[Native dependencies](native-dependencies.md) records versions, provenance and
licenses. The SDL aggregate ABI manifest generator remains a separate setup tool
because it describes installed SDL types. See [SDL usage](sdl.md).

Components and contracts
------------------------

Source paths below are relative to `shared/`.

### line-edit.c / line-edit.h

Bestline terminal/callback adapter. One process-wide editor; snapshots are
copied before reading. The idle callback services the dev server while input
waits. Header comments describe returned-string and callback lifetimes.

### ffi/native.c / ffi/native.h

Compiler ABI facts and dynamic-loader adaptation. Library handles remain alive
for the process lifetime, including across Otium namespace reloads. Runtime
adapters own managed values; shared code never stores their pointers.
[Aggregate ABI notes](ffi-abi.md) describe by-value call lowering.

### dev/transport.c / dev/transport.h

POSIX socket/poll adapter with bounded framing queues. One server per process,
on the interpreter thread. Invalid UTF-8 and embedded NUL disconnect a client
before a payload crosses the native-string boundary. Evaluation, response
construction, interruption, socket naming and reload behavior live in
dev/core.ot. Its record-load! hook owns file/load-time records for both runtimes;
runtime adapters expose raw binding facts and Otium derives public arity.
The native private-directory helper only creates/checks a supplied POSIX path;
it does not choose directory names or socket names.

### support/utf8.c / support/utf8.h

One byte-span validator for foreign-string ingress, shared by FFI and transport.
Embedded NUL is valid Unicode; transport applies its own no-NUL protocol rule.
Keep this support directory limited to actual boundary needs. It is not a home
for language algorithms or a replacement for the Otium standard library.

### ui/layout.cpp

Yoga's native callback/struct interface, exposed as opaque nodes and scalar
queries. Measurement is replayed with answers supplied by Otium; managed
pointers and interpreter callbacks do not enter Yoga's callback. Query indices
belong to the current measurement pass. Free nodes after detaching children.

### ui/text.c / ui/text.h and ui/renderer.c

SDL_ttf custom text-engine and SDL GPU interfaces. All calls run on the SDL
thread. Renderer handles borrow windows; destroy renderers before windows and
before SDL_Quit. Event payload strings last until the next poll and Otium copies
them immediately. Text handles are borrowed cache entries (see text.h).
Last-renderer/last-node teardown clears shared text resources. Namespace reload
must preserve live handles until explicit teardown; it must not unload the DSO.

Existing policy to migrate
-------------------------

These files are not all compliant merely because they are now built together.
The next reductions should retain tests of their observable behavior:

* line-edit.c: token scanning, completion selection, help rendering and editor
  preferences belong in Otium. Keep Bestline callback and terminal/signal handling
  in the bridge; expose a request/result boundary that works on both runtimes.
* ui/text.c: font selection, cache replacement and ligature caret interpolation
  are policy. Expose text/cluster lifetimes and scalar cluster data, then move
  these choices to Otium. A cache's speed is not a justification for keeping it C.
* ui/renderer.c: atlas packing, primitive batching, per-window event routing and
  key naming need the same treatment. Retain only the GPU/SDL ABI operations that
  cannot reasonably be expressed through FFI. Measure Otium implementations to
  find runtime/compiler improvements, not to grant native exceptions.
* dev/transport.c: framing queues and limits currently travel with POSIX polling.
  A byte-buffer/IO boundary would let Otium own framing without constructing
  strings from unvalidated bytes. Preserve malformed-input and backpressure
  behavior when changing this boundary.

Application metadata, window icons and foreground selection now live in
shared/lib/otium/ui/gpu.ot and call SDL through otium.sdl. This is the pattern for
ordinary library orchestration: bind the library directly, keep policy reloadable,
and use unwind-protect to release resources on failure.
