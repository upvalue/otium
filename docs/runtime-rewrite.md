# C runtime

The C runtime is Otium's second, experimental implementation; Chez is the
primary one. It aims at small memory use and embedding: a moving collector, a
bytecode compiler and VM, and libffi for foreign calls. A C compiler is needed
to build it and its test fixtures, but not to run it.

It was written clean-room, from the public language docs, library, examples and
tests rather than the previous C runtime. Keep it that way: don't consult that
runtime's sources in repository history (anything under `c/src` before commit
62cd5ff). Resolve semantics from the public docs, library, tests and Chez.

## Layout

- `c/include/otium.h` is the embedding API, built around `ot_state`. It is the
  only header a host needs.
- `c/src/` is the runtime. `main.c` is the command-line program, built as
  `build/otium`. `primitives.ot` is Otium source loaded before the core
  library. `runtime-reference.inc` is generated from `lib/catalog/runtime.json`
  by `tools/generate-reference.py`.
- `c/tests/unit/` holds the unit tests. Each `NAME.c` is one suite, and all of
  them link into one runner, `build/c/otium-unit-tests`. `support/` has the
  test framework and helpers, `hosts/foreign-control.c` is a host program for
  callback checks that end the process, `fixtures/` has an extra FFI library,
  and `data/` has Otium scripts the suites load.
- `c/tests/tests.py` defines the C runtime's repository tests: the conformance
  files in `c/tests/otium/`, the CLI, REPL, server and embedding host.
- `c/bench/runtime.c` is the warm microbenchmark program behind `just bench-c`.
- `c/tools/amalgamate.py` writes the single-file distribution (`otium.c`,
  `otium.h` and `main.c`). `c/tools/format-c` runs clang-format over the C
  sources, and `c/tools/gc-lint/` is the managed-pointer analysis.
- `c/meson.build` builds all of these; the root `meson.build` includes it.

## Building and running

`just build` sets up `build/` and compiles, the same as
`meson setup build && meson compile -C build`. Put other configurations in
`build-NAME/`. Two Meson options are specific to this runtime:

- `-Druntime_checks=kinds` checks the kind behind every object struct cast;
  `roots` also moves the heap and VM stack so a missed root faults.
- `-Dvm_switch=true` builds the VM with switch dispatch instead of computed
  goto.

Run a file with `./otium --runtime=c file.ot` or `build/otium file.ot`. The
launcher takes the options in `lib/catalog/options.json`, the same as Chez, plus
`-e SOURCE` to print an expression's result. `./otium --help` lists them; those
starting with `--c.` apply only to this runtime. The heap starts at 1 MiB per
semispace and can grow each to 64 MiB; `--c.heap-init` and `--c.heap-max`
change that, and the two spaces together use twice the size. `--c.max-depth`
caps live VM frames, and the stack is capped at 128 Ki value slots.
`--c.gc-stats` and `--c.gc-stats-json` print collector statistics at exit.
`OTIUM_GC_STRESS=1` collects at every allocation in any build.

`meson compile -C build c/otium-amalgamation` builds the single-file
distribution and an executable from it.

## Current architecture and limits

`compiler.c` compiles bytecode and `vm.c` executes it. `modules.c` owns module
loading; macro expansion and higher-level library behavior use shared Otium.
`internal.h` holds runtime implementation interfaces, including a struct for
each object kind that mirrors its heap layout (`oti_pair_obj`, `oti_class_obj`,
...). The builtin ID enum is in `internal.h`; each source file has an
`oti_builtin_def` table naming its builtins, and `builtins.c` combines them.
Each module's handler switches on explicit IDs, so the enum's order doesn't
matter.

`foreign.c` owns loading, managed wrappers, native memory, call conversion and
condition context. `foreign_types.c` owns scalar descriptions, recursive libffi
ABI trees and signature preparation/cache lookup. Its private `foreign_types.h`
is separate from the embedding API. Type preparation uses host allocation only.
Signatures own their ABI trees; pending wrappers pin signatures across managed
allocation. Binding and memory list links are published after allocation, when
the current head is known. The collector retains four unused signatures for
reload reuse, plus signatures pinned by live bindings or pending wrappers.

The public FFI tests in `tests/ffi/` pass: common 222 checks, callbacks 51,
ownership 10 and bulk 23. Union calls are supported on x86-64 Linux SysV; other targets raise
`abi`. Union lowering merges every member's integer/floating leaves, preserving
offsets and padding. It follows the [AMD64 ABI classification rules](https://gitlab.com/x86-psABIs/x86-64-ABI/-/raw/master/x86-64-ABI/low-level-sys-info.tex)
and [libffi's x86-64 classifier](https://github.com/libffi/libffi/blob/master/src/x86/ffi64.c).
The exposed types exclude vectors and long double.

Callbacks use libffi closures with rooted procedures and deferred conditions.
VM reentry preserves the suspended caller and its dynamic handlers. Errors and
exits stop at the callback boundary and resume after the outermost native call;
escaping restarts become `escape` conditions. Foreign-thread and idle-host calls
abort before entering the VM. A callback released during a native call keeps its
trampoline until that call returns. Released metadata is reclaimed after its
wrappers disappear. Both collection-safety flags allow same-thread callback GC.

`ffi/own` retains a pointer and custom destructor until explicit release. The
owner is marked released before calling the destructor, including error and
recursive-close paths. Views share owner invalidation. Only owning pointers
dispatch as `CPointer`; callbacks and borrowed pointers need explicit release.
C pointer results have unknown extent and no inferred allocation owner. Address
comparison remains valid after release. Runtime teardown frees native allocations
and callback closures but does not call Otium custom destructors.

The `foreign` unit suite runs the unchanged common, callback, ownership and
bulk scripts with collection at every allocation. It also checks callback
cleanup, native function pointers, self-release, retained procedures, 100
callback replacement cycles and external ownership. The `c foreign host` test
runs `c/tests/unit/hosts/foreign-control.c` to check wrong-thread and idle-host
rejection and deferred exit with outer cleanup.

FFI memory errors identify the operation and the validation argument, with scalar
expected types where applicable. Shared field helpers annotate conditions at the
signal site, preserving a single handler invocation. Loader condition messages
retain all attempted candidates even when they exceed the short runtime display
buffer. `c/tests/unit/data/foreign-errors.ot` adds 32 standalone checks. The
common suite also passes on Chez.

The port embedding API is `ot_io_host` with `ot_set_io_host`. Hosts supply
open/read/write/close callbacks and an optional flush hook. Read/write return
counts or negative errno values; other callbacks return zero or errno. The
runtime copies the table and snapshots it with userdata into each new port.
Install it before loading the core to replace standard handles 0/1/2, which are
borrowed. Existing ports keep their original host after replacement.

File and standard ports buffer in managed bytevectors. Native host callbacks
borrow those bytes and cannot reenter Otium or cause collection; the public
entry points reject such attempts before touching VM state. Custom Otium ports
continue to receive rooted bytevectors and may allocate. Partial writes retain
progress across errors. EINTR polls the interrupt flag before retrying, preserving
partial UTF-8 input. Destruction drains native buffers and closes owned handles
without running Otium callbacks; use `ot_flush_ports` first to observe errors.
The default host uses POSIX descriptors. The REPL and program reads share the
standard input buffer, and each echoed result is flushed before the next form.

The `io-host` unit suite exercises these contracts with a host that returns
one-byte reads and two-byte writes, injects errors and interrupts, checks that
borrowed buffers belong to the moving heap, and attempts rejected reentry.
Filesystem metadata and source/module loading still use POSIX directly, so port
host support does not yet give filesystem-free embedding.

The embedding API has no way for a host to define native functions or make
foreign values. Native code is reached through `otium.ffi`.

## Errors

A failing operation records an `oti_error_record` in `rt->error` and returns
false or zero. Recording never allocates on the managed heap, because callers
often hold unrooted values and return straight after. The condition is built
where the error is caught, by `oti_error_condition` or `oti_condition_of`.

The record holds the message, owned and not truncated, a diagnostic code, the
source offset and typed fields. `who` and `tag` become the condition's `:who`
and the symbol that starts `:data`, as in Chez; up to three further `:data`
values live in rooted slots, so storing one is safe anywhere.
`oti_tagged_error` records these, and generic dispatch, slot access and class
and generic declarations use it, so `((get e :data) 0)` is a symbol such as
`missing-method` on both runtimes. `ffi_reason` marks an FFI error, which
`foreign_run` turns into an `ffi-error` condition; `in_module` marks a message
that already names the module whose load failed.

`oti_error`, `oti_errorf`, `oti_coded_errorf` and `oti_ffi_errorf` record an
error; the first one wins. `oti_failed(rt)` tests for one. Code that runs Otium
while an error is pending, such as cleanups and port flushing, moves the record
out with `oti_take_error` and back with `oti_restore_error` or
`oti_drop_error`.

The record also says where the error happened, in host memory: a source id
and offset once something locates it (`oti_error_at`), the frames of an
uncaught error, and the top-level forms in other files that led to it.
Source ids index `rt->sources`, the paths of required files; 0 is the text
the host passed to `ot_eval`, and `OTI_NO_SOURCE` marks runtime libraries and
evaluated data. Each function keeps a cold table from instruction indexes to
the offsets of the forms they came from, filled as the compiler emits code,
and its name. When dispatch fails with no try frame installed, `oti_drive`
records the frames (`oti_trace_error`) before anything unwinds; a frame whose
tail call failed is left out, since on Chez the call has replaced it. When a
top-level form fails, `oti_locate_error` locates an unlocated error at the
form, or notes the form when the error is located in another file. Lists read
for the shared expander carry their offset in their first pair, and
`expander-locate` copies it onto rewritten forms, so expanded code keeps its
position.

`ot_report_error` turns the record into a diagnostic table, adding `:span`,
`:frames`, "reached from" notes and `:suggestions` from the source files as
Chez's `locate!` does, and renders it with `otium.diagnostics/render`, falling
back to plain text. Messages follow Chez's and the catalog's: E0201 has
`:name`, E0301 has `:name` and `:label`, and read errors use the catalog
message.

## Launcher paths and options

The launcher supports upward project.ot discovery, --project/--no-project, and
colon-separated OTIUM_PATH. Explicit -L/--path entries precede environment and
project paths. Project files are parsed as data: only top-level (paths ...)
strings contribute directories, resolved relative to the project file. Comments
and unrelated settings do not become search paths. Malformed paths forms fail;
project contents are never evaluated.

Options come from `lib/catalog/options.json` through the table that
`tools/generate-cli.py` writes to `c/src/options.inc`, so `--help` is the same on
both runtimes and errors name the option the same way. Chez-only options are
accepted and ignored. Numeric options reject signs and whitespace. Attached
--path, --load and --project values work. Preload-only invocation stops after
loading unless --repl was requested. `-e` is a C-only extra and isn't in
`--help`.

The prototype's own `--heap`, `--heap-max`, `--stack` and `--stats` are gone.
`--c.heap-init`, `--c.heap-max`, `--c.max-depth` and `--c.gc-stats` replace them.
Without `--c.heap-max`, the limit is 64 MiB or the initial size if that is
larger. `--c.max-depth` is applied by never growing the frame array past it, so
calls pay nothing for it. `--c.gc-force-compact` was dropped: the semispace
collector compacts on every collection. `--c.gc-stats-json` reports allocations,
collections, allocated, copied and reclaimed bytes, current and peak use, and
the semispace capacity and limit. The counters change only during collection;
the allocation count is the object identity counter.

The REPL reads from the standard input port, which it shares with programs,
and prompts only on a terminal. On a terminal, and with `--dev`, it reads
through the line editor in `native/line` instead, as Chez does, and an
interrupted evaluation returns to the prompt.

## Development server

`--dev[=PATH]`, `--dev-watchdog=MS` and `serve` work as on Chez; see
`specs/dev-server.md`. The protocol is `lib/otium/dev.ot`. The C runtime links
the socket transport from `native/dev` and provides the primitives that file
uses: the transport, clocks and namespace listings in `c/src/dev.c`, and request
evaluation, output copying and the pause logic in Otium in `c/src/dev.ot`, which
the launcher loads after `lib/otium/dev.ot`.

Pausing uses `ot_set_interrupt_handler`. When the interrupt flag is set and a
handler is named, the VM calls it at its existing safe points (backward jumps
and calls) and blocking port reads call it while they wait; other places that
check the flag leave it set. The handler gets the flag's value: 1 for SIGINT and
2 for a timer tick. The launcher sets up the tick with `ITIMER_VIRTUAL` every
10 ms, so it runs only while the process uses CPU and never interrupts a
blocking call. On a tick the handler polls the socket and checks the watchdog
deadline, and pauses in `dev/break!` inside `continue` and `abort` restarts.
Aborting raises an error in the paused code, as on Chez.

`ot_set_load_hook` names a function the module loader calls before loading a
file; the server passes `dev/record-load!`, which the introspection functions
use to find source files.

`--c.server` reads frames ending in a line holding only `\x1f` and answers each
with output, the value and a `\x1eot> ` prompt, through `dev/stdio!`. SIGINT
while a frame runs pauses it with the same restarts and serves frames until one
invokes a restart; a second SIGINT, or `abort`, interrupts it.

## Checks

`just test c` runs the C suite: the unit runner and the tests in
`c/tests/tests.py`. `just test` runs the default suites for both runtimes. To
run one repository test without meson, use
`OTIUM_BUILD=$PWD/build PYTHONPATH=$PWD/tools:$PWD python3 tests/run 'NAME'`.

Run the unit runner from the repository root, since suites load files by
relative path. `build/c/otium-unit-tests` runs every test. Arguments select
tests: one containing `/` is a prefix of `suite/test`, as in `reader/` or
`foreign/common_ffi_script`; anything else matches part of the name. Each test
runs in a child process; `--no-fork` runs them in the runner itself, which is
easier in a debugger.

`just sanitize` builds `build-asan/` with ASan, UBSan and
`runtime_checks=kinds`, then runs the C suite. `just check-roots` builds
`build-roots/` with `runtime_checks=roots`, where every collection copies into
freshly mapped memory and protects the old space, and the VM stack moves to new
memory on every `oti_stack` call. It then runs the C suite collecting at every
allocation, so a value or object pointer held across an allocation, or a
pointer into the stack held across growth, faults instead of reading a stale
copy. Both set `ASAN_OPTIONS=detect_leaks=0`, so neither checks for leaks.
`just test-stress` runs the default tests collecting at every allocation, which
is slow.

When changing allocation, roots or managed pointers, also run `just gc-lint` if
the LLVM development tools are installed. `c/tools/format-c --check` checks C
formatting.

## Reading the milestones

The memory and execution contracts describe runtime invariants. The implementation
sequence and dated development sections below are historical snapshots. Earlier
claims about missing language features, generated-C commands and native timing
measurements describe those milestones, not the current executable. The native
tier was removed during VM-first compliance work. Later entries supersede earlier
ones, including the FFI limitations recorded during each stage.

Everything from here on was written while the runtime lived in `prototype/`, so
its paths, the `prototype/otium-proto` program and the `make -C prototype`
targets refer to that old layout. The files are now in `c/src/` and the tests in
`c/tests/unit/`, and the scripts it names, such as `compliance.py`, are gone.

## Cleanup validation

The cleanup baseline passed the full prototype test target and all 49 core
inventory groups. C sources and headers were formatted with clang-format 23.1.3
and the repository configuration. Repeated formatting converged to identical
output; the consecutive MEMORY_CASE invocations needed multiple passes.
Formatting was saved separately from structural changes for review.

After refactoring, the full prototype suite and all 49 core inventory groups
pass again. Chez differential checks pass for 74 programs and 50 errors; FFI
differential checks pass for 12 programs and 6 errors. Compiler-free runtime and
foreign checks pass. Foreign, foreign-reload and runtime ASan/UBSan targets pass,
including 23 bulk and 39 aggregate/error checks. Leak detection was disabled;
these runs do not validate leaks. Optional LLVM development tools were unavailable
for GC lint. All 268 public builtin ID/name pairs retain their original order.

Further factoring remains possible in `foreign.c`: `shared_builtin` and
`foreign_builtin` still combine native-memory validation with operation dispatch,
and `layout_copy` and condition-context helpers remain in that module. Their
rooting and error-order contracts warrant separate changes.

## Memory contract

Values occupy one machine word. Odd values are signed integers with one tag bit;
small even immediates represent nil, booleans and the empty list. Aligned nonzero
pointers identify managed objects. Integers must fit in the signed word minus
one bit; arithmetic overflow handling belongs in the numeric layer.

Objects have a size, a kind and a traced-field count. Traced fields precede opaque
bytes. The collector copies reachable objects breadth first into the other
semispace, preserving cycles and sharing through forwarding addresses. Collection
uses no recursive traversal or auxiliary allocation. Each object has a 16-byte
header on 64-bit systems. Pairs therefore currently occupy 32 bytes.

Root frames describe contiguous arrays of values. Allocation is a safepoint:
callers must root every managed value needed afterwards and reload pointers from
their root slots afterwards. Interior pointers cannot survive allocation. All
managed fields must contain valid tagged values. Root frames must be popped in
reverse order. Heap operations are single-threaded.

Embedding heaps start with a fixed budget of two semispaces. A host can opt into
bounded growth with ot_set_heap_limit. Allocation collects first, then grows
only if the surviving objects and request cannot fit. Growth doubles capacity
up to the configured limit. Both replacement spaces are allocated before any
root is changed; allocation failure leaves the existing heap usable. The old
and new spaces coexist briefly during relocation, so transient host allocation
can exceed twice the configured semispace capacity. Allocation returns zero if
the request still cannot fit within the limit.
Opaque payloads are zeroed; traced fields start as nil. The compiler should
eventually expose object initialization to optimization so unnecessary writes
can be eliminated without weakening the safepoint contract.

## Historical implementation sequence

1. Precise compacting heap: implemented, with relocation, cycles, sharing,
   repeated collection, payload integrity and exhaustion tests.
2. Reader: implemented against the public datum fixtures. All 58 accepted values
   and 55 rejected inputs pass. Arrays, tables, bytevectors and reader prefixes
   become constructor forms. Compiler nodes use a bounded arena separate from
   the moving runtime heap; token text borrows the source buffer and decoded
   strings live in the arena. Errors retain a source byte offset. Nesting is
   bounded at 512 levels. Integers have a signed 64-bit reader limit; the compiler
   rejects values outside the runtime's narrower immediate range.
   The library, examples and benchmark workloads parse (67 files), but this does
   not establish that they execute.
3. Bytecode compiler and VM: implemented for lexical bindings, fixed/rest/optional-arity
   functions, closures, captured mutation, internal definitions, named let,
   conditionals, short-circuit logic, while and proper tail calls. Global loads
   use indexed slots so existing callers observe redefinition; saved function
   values retain their code. Supported values include immediate integers,
   floating-point numbers, strings, symbols, keywords, characters, pairs, mutable arrays and ordered tables.
   Primitive operations cover arithmetic, comparisons, pair/list construction
   and mutation, predicates and printing. Arithmetic reports integer overflow.
4. Native compilation: implemented as a tier that verifies bytecode stack
   heights and lowers control flow to C. The host compiler optimizes the result.
   Integer arithmetic/comparison, pair and array operations have guarded fast paths;
   generic operations retain the VM semantics. More specialization and better
   call lowering are still needed for the performance target.
5. Basic FFI: implemented for fixed scalar signatures, UTF-8 strings, pointers
   and owned native buffers. Typed C adapters share the native module compiler.
   Bindings and buffer operations work from both interpreted and native code.
6. Collections, macros and loading sufficient to run the shared library and
   representative public tests remain to be implemented.
7. Compare matched workloads with Python, Ruby, Lua and Chez. Record correctness,
   steady-state time, startup/compilation time, peak memory and collector work.
   Include numeric loops, recursive calls, closures, tables and allocation-heavy
   workloads. Performance completion requires measured wins across representative
   workloads, not a single numeric microbenchmark.

Generational collection remains a measurement-driven option. Namespaces/loading,
macros and the condition system remain
unimplemented. The shared library cannot execute yet. FFI callbacks, aggregate
layouts/calls and variadic calls are also unimplemented.

## Execution contract

Each instruction occupies eight bytes. Function metadata, instructions and
capture descriptors live outside the managed heap; constants and globals are
rooted value arrays. A frame's locals and evaluation operands occupy a single
rooted VM stack. Stack growth updates its root pointer before any managed
allocation. Functions have indexed local slots. Only captured bindings receive
heap boxes, so ordinary numeric arguments and loop variables do not allocate.
Closures contain references to those boxes and a nonmoving function index.

Calls preserve the caller's pending operands on the VM stack. Tail calls move
the new closure and arguments over the current frame, including calls between
functions with different arities. The dispatch loop uses no recursive C calls.
The million-iteration loop and mutual-recursion tests use at most two frames.

Top-level forms compile and execute in source order. A later read, compile or
runtime error leaves earlier definitions in place. Failed compilation discards
its new functions and constants. Runtime errors unwind VM frames and leave the
runtime available for further evaluation. This is a simple error interface,
not Otium's condition system.

Code and constants are reclaimed before compiling each top-level form. Live
closures retain their functions, nested-function definitions and constants;
unreachable wrappers and superseded definitions release their native modules.
Metadata slots are reused without renumbering live code. Stack liveness analysis
and further foreign ownership work remain necessary for a long-running,
low-memory runtime. Heap limits do not include compiler arenas,
code, global names or the VM stack. Runtime errors currently have messages but
no instruction-to-source mapping; compile/read errors include source offsets.

## Historical native tier

The native tier currently targets POSIX hosts with a C compiler and `dlopen`.
It creates a private temporary directory, emits C from verified instructions,
invokes the compiler directly without a shell, and loads the resulting shared
object. Temporary source and library files are removed after loading. Libraries
remain mapped until runtime destruction. `OTIUM_NATIVE_CC` selects a compiler
executable; it is not a shell command or flags string. Both GCC and Clang have
been exercised. The build records the absolute prototype header directory and
exports runtime symbols with `-rdynamic`; native compilation therefore currently
requires that source directory to remain available.

Bytecode operands become fixed stack offsets and branches become C labels.
Before each allocating operation or runtime call, generated code publishes the
exact active stack height. Managed values live in the rooted VM stack, and
generated code reloads them after allocation. The private-frame loop path below
cannot allocate or trigger collection. Object field accessors are inline
in the shared value ABI. The C compiler can optimize arithmetic and control flow
without hiding managed pointers from collection.

Native and interpreted functions use the same frames and may call each other.
General calls return to the dispatch loop. Fixed-arity native self calls with
unboxed parameters may nest directly up to 32 levels; deeper calls use the
dispatch loop. Calls to the same closure in tail position jump to the generated entry
label when parameters are unboxed. Captured parameters use the general entry
path, which creates fresh boxes for the new invocation. Mutual tail calls and
calls with different arities retain the general frame-reuse path.

Fast paths check the actual callee value saved before argument evaluation. This
preserves both global redefinition and calls whose arguments redefine their
callee. Non-integer arithmetic and replaced builtins fall back to normal runtime
calls. Integer overflow remains an error. Native compilation failure is reported
as a runtime error; native execution is optional and the VM remains available.

The tier does not yet use a typed SSA representation, direct native calls between
functions, escape analysis or unboxed floating-point loops. Ordinary recursion
between different closures still crosses the shared dispatch loop. These are
performance gaps, not evidence that native compilation has met the goal.

## Foreign calls

`prototype/example-ffi.ot` calls libc's `strlen` and exercises native storage.
The prototype provides `ffi/...` globals directly; it does not yet load the
`otium.ffi` module or implement its macros. Supported entry points are:

- `ffi/open-library`, `ffi/has-symbol?`, and `ffi/bind`, with the public library's
  five-argument binding signature, including a strict boolean collection flag.
- `ffi/null`, `ffi/null?`, `ffi/pointer?`, and `ffi/pointer=?`.
- Scalar `ffi/sizeof`, `ffi/alignof`, `ffi/alloc`, `ffi/ref`, `ffi/set!`,
  `ffi/view`, and `ffi/free!`.
- `ffi/alloc-utf8`, `ffi/read-utf8`, `ffi/allocation-count`, and
  `ffi/allocation-bytes`.

Scalar names and conversion rules come from `lib/otium/ffi.ot` and the public
FFI tests. Fixed-width and platform-width integers, booleans, float, double,
pointer, UTF-8 and void results are supported. Callback-pointer arguments accept
only the null placeholder. Bindings require accurate fixed C signatures;
aggregate and variadic declarations are unsupported. Argument counts are limited
to 64. Integers outside the runtime's immediate range are rejected on return.
Void calls return `#f`, matching the public fixture tests.

Foreign bindings use libffi call interfaces cached by argument/result types.
The build obtains libffi flags through pkg-config. Arguments use their declared
C representations, including narrow integers; integral returns narrower than
ffi_arg are decoded from libffi's widened result storage. Binding and calling
foreign functions no longer generates C or starts a host compiler. The generated-C Otium tier has also been removed.

Calls are synchronous leaves. The collection-safety flag is validated and stored;
callback/reentry and concurrent collection are not implemented. Managed pointers
never cross this boundary. UTF-8 arguments receive native copies for the duration
of the call, and returned UTF-8 is validated and copied before those buffers are
released. A pointer result into a temporary argument copy is rejected. Native
code that retains data must receive persistent storage such as `ffi/alloc-utf8`.

Owned buffers are zero-initialized and have checked sizes, alignment and bounds.
Views share their owner's lifetime. Known returned aliases are associated with
the containing allocation; freeing the owner invalidates those aliases and its
views. `ffi/ref` and `ffi/read-utf8` require bounded storage. Unknown foreign
pointers can be passed back to C but cannot be dereferenced by those operations.
Only the owning pointer can be freed through `ffi/free!`; repeated frees return
`#f`. Allocation counters describe live owned buffers, excluding temporary call
copies and adapter metadata.

Foreign bindings and released allocation records are reclaimed after moving
collection. Unused call interfaces have a bounded cache; live bindings retain their
interfaces. Reopening the same loader handle reuses one library record. Libraries
remain mapped until runtime destruction because raw pointers may outlive their
managed wrappers. Explicit `ffi/free!` releases the buffer immediately;
collecting a managed pointer wrapper does not free native storage. Remaining
buffers are released at runtime destruction. Ownership finalization and explicit
library lifetime control remain future work. FFI errors currently use the
runtime's simple error interface, not Otium condition objects.

## Initial timing baseline

Run `python3 prototype/benchmark.py` after building. `baseline-vm.json` records
three samples per workload/runtime for recursive Fibonacci, a numeric tail loop
and transient pair allocation. The script checks every result. Comparisons
include Python, Otium on Chez, and direct Chez Scheme when `scheme` is available.
The Scheme pair workload uses named let in place of Otium's while form.

These are startup-inclusive wall times, not steady-state measurements. The
Otium-on-Chez launcher has substantial startup cost on these short workloads.
Direct Scheme timings help expose the execution gap without that library load.
This baseline does not establish the requested performance target. Ruby and standalone Lua executables were unavailable. LuaJIT library-based
comparisons have since been added below.

For warm measurements, build `make -C prototype bench_runtime` and run
`python3 prototype/benchmark_steady.py`. `baseline-native.json` records five
samples after two warmup calls for the VM, native tier, Python and Chez Scheme.
The runtime harness invokes an existing global through `runtime_call`, without
reparsing or compiling a wrapper. It checks every answer and rejects a run if
native compilation occurs during a measured sample. Native compiler time is
reported separately. Linux peak RSS is recorded for the prototype and Python;
prototype RSS excludes the external compiler process. Collector and frame
statistics include warmup calls. Broader workloads and full low-memory validation
remain necessary.

`python3 prototype/benchmark_foreign.py` measures a warm million-call integer
FFI loop against Python ctypes and Chez's direct foreign-procedure interface.
`baseline-foreign.json` records the results. The shared C fixture, argument,
expected sum and warmup/sample counts match. Measurements include the language
loop and marshalling, while native/adapter compilation occurs before sampling.

## Arrays and structural equality

Arrays have a stable managed wrapper and a separate traced backing store. Growth
starts at four slots and doubles capacity. Resizing reloads both the wrapper and
arguments after collection. Pop clears the vacated slot so removed objects can
be reclaimed. Copy shares elements but owns separate storage. Empty arrays need
no backing allocation. The wrapper costs 40 bytes on a 64-bit host; nonempty
backing storage costs a 16-byte header plus one word per capacity slot.

The VM, direct call API and native tier support callable arrays, optional lookup
defaults, mutation, shallow copy and structural membership. Native calls guard
the saved builtin value before inlining indexed lookup, length and append into
spare capacity. Growth and invalid indexes fall through to the shared checked
implementation. Global redefinition during argument evaluation retains the
callee chosen before evaluating those arguments.

Structural equality traverses pairs and arrays using a host work list and a
hash set of visited object pairs. This avoids recursive C stack growth and
terminates on cycles. It performs no managed allocation, so heap addresses stay
valid for the duration of the operation. Temporary host memory scales with the
comparison, outside the semispace budget. Exact and inexact numbers remain
distinct for equality. Cyclic-array equality did not terminate in the Chez
probe; prototype cycle handling is a robustness extension, not a differential
compatibility claim.

Array checks cover aliasing through growth, nested mutation through shallow
copies, collection at every allocation, clearing popped slots in an 8 KiB
semispace, failed-growth recovery, 20,000-element list equality and cycles.
General sequence coverage remains incomplete: length/get/copy/contains? cover
arrays and tables, plus nil lookup and nil/empty-list length. List and string
operations beyond structural equality still need work.

The warm benchmark now includes building a 50,000-element array and summing its
indexed elements. Python uses list append/indexing; Chez Scheme uses a vector
that doubles capacity and copies its old elements. The Chez kernel is direct
Scheme rather than the Otium collection layer. Compilation remains outside the
measured samples. See baseline-native.json for results and runtime metadata.

In the recorded run, the array kernel takes 0.368 ms in the native tier,
7.435 ms in the VM, 1.708 ms in Python and 0.367 ms in direct Chez Scheme.
Guarded array operations reduced the native kernel from 1.095 ms before those
fast paths. At that stage Fibonacci still trailed Python. Later call-path
changes are described below; the overall performance goal is not achieved.

## Ordered tables

A table has a 48-byte managed wrapper and one managed backing object. The backing
object holds traced key/value slots in insertion order and opaque 32-bit bucket
indexes. Capacity starts at eight entries and doubles; the bucket count is twice
the entry capacity. Storage costs 16 bytes plus 24 bytes per entry-capacity slot
on a 64-bit host. Growth temporarily retains the old and new backing objects.
This transient memory must fit in one semispace. Empty tables need no backing
allocation.

Lookup uses open addressing. Deletion clears both traced fields and leaves a
bucket tombstone. Updating an existing entry preserves order; deleting and
reinserting appends it. When the entry area fills, tables with enough deleted
entries compact at the same capacity instead of growing. Copy duplicates the
backing store and shares keys and values. Keys and values return arrays in
insertion order. Tables and keywords are callable, with optional defaults.

Object headers retain their 16-byte size. The kind word now holds an eight-bit
kind, a 23-bit identity hash and one frozen flag. Identity hashes survive copying
collection; wraparound can cause collisions but does not make distinct keys
equal. Arrays, tables and other opaque objects use identity as keys. Strings,
symbols, keywords and numbers use value equality, with exact/inexact numbers
kept distinct. Pairs use structural key equality, including identity semantics
for nested mutable collections. Table equality itself compares identity.

Pair keys freeze recursively through pairs, even for unsuccessful lookup or
nil-valued insertion, matching Chez. Mutating a frozen pair reports an error.
Freezing stops at mutable arrays/tables and uses an explicit work list. Pair
hashes inspect a bounded structural prefix (eight levels), keeping cyclic and
deeply nested keys finite. Collisions use full cycle-aware equality. Keys with
long common prefixes can therefore collide more often; this needs broader
benchmark coverage. No managed pointers live in untraced bucket storage.

Native get/put! calls guard the saved builtin and table type before calling
shared table helpers directly. Set publishes the exact root range before any
possible resize. Tests cover global replacement during argument evaluation,
root relocation, compaction order, frozen keys, cyclic keys, hash-value/identity
semantics, shallow copying, churn in 8 KiB and recovery after failed growth.

The tables10000 warm kernel inserts 10,000 integer keys with doubled values and
then sums indexed lookups. Python uses a dict and direct Chez Scheme uses an
eqv hashtable. Chez's benchmark table is unordered, while the prototype and
Python preserve insertion order. The prototype uses a 1 MiB semispace (2 MiB
across both spaces); host metadata and compiled code are additional memory.

The recorded table run takes 0.458 ms native, 1.832 ms in the VM, 0.512 ms in
Python and 0.301 ms in direct Chez. Direct helper calls did not demonstrate a
clear speedup over the initial 0.455 ms native measurement. The public
100,000-entry tables workload also completes in VM and native modes with an
8 MiB semispace. The public array-equality tests run with a minimal check function
in place of their unavailable test-module import; all checks pass in both modes.

## Call-path overhead

Call entry now checks existing stack/frame capacity before calling growth
helpers. Non-tail arguments already occupy their destination slots, so entry
skips the no-op memmove. Return writes into the existing closure slot and
publishes the new root count without calling the stack-growth path. Native
continuations also skip the stack helper when their full operand range fits.
Stack-limit checks remain independent of allocated capacity.

Generated non-tail self calls have a guarded frame-entry path when their
parameters are unboxed. The guard compares the saved callee with the current
closure, preserving redefinition semantics. The path grows stack/frame storage
when needed, initializes remaining locals and publishes roots. It then calls
the private generated body directly while native nesting remains below 32.
Beyond that limit it resumes through the ordinary dispatch loop. Boxed parameters
and calls to other closures retain shared entry handling.

Tests include retained managed values across recursive calls, argument boxing,
callee replacement during argument evaluation, moving collection at every
allocation, tail-space bounds, stack-limit recovery and mixed VM/native
execution. The full runtime suite passes in VM, eager-native and tiered modes;
ASan/UBSan instrument both host runtime and generated modules. Ruby and standalone Lua executables remain unavailable. The LuaJIT shared
library is now measured as described below.

Alternating three runs of saved pre-change and current benchmark executables
(15 warm samples each) gives Fibonacci medians of 57.319 ms and 24.384 ms,
a 2.35x improvement. The full refreshed baseline records 25.299 ms native,
43.242 ms Python and 2.850 ms direct Chez for Fibonacci. Native now beats
Python on this kernel, but remains about nine times slower than Chez. See
baseline-native.json for all five matched kernels and individual samples.

## Code and constant lifetime

At evaluation boundaries, reclamation walks managed roots and live code together.
A closure reaches its function; bytecode reaches constants and nested-function
metadata, including child closures that have not been instantiated yet. A heap
bitmap and explicit work list handle sharing and cyclic data. Unreachable code
releases bytecode, capture descriptors and native modules. Unreferenced constant
slots become nil before the semispace collector runs.

Function and constant indexes remain stable while live. Free slots are reused,
and trailing holes shorten the tables. Equal immediate values and identical
managed values share constant slots. Symbols and keywords have weak intern
entries: live user data retains their identities even when the code that first
created them has disappeared. FFI's null constant remains pinned. Compilation
tracks new slots transactionally, so rollback clears only the failed form's
allocations, including reused slots.

Reclamation runs with no executing frames and does not change runtime_call's
warm execution path. It adds a graph walk and collection before each source
form, which can make large source loads more expensive; a pressure-based policy
may be preferable later. Scratch marking storage, metadata capacity, reader
arenas and compiled code are outside the semispace budget. Metadata arrays keep
their peak capacity. Foreign metadata has the separate collection policy
described below; newly introduced global names remain allocated.

make -C prototype test-reload runs 2,000 VM redefinitions and 40 eager-native
redefinitions in an 8 KiB semispace, with collection at every allocation. It
checks saved old closures inside cyclic tables, nested code not yet invoked,
symbol/keyword identity, slot reuse and rollback after partial compilation.
Observed slot counts stay at seven functions and nine constants; managed bytes
sampled after each iteration peak at 768 in the VM and 760 in native mode.
These are sampled boundary values, not allocation-time peaks. Dropping the old
closures frees their metadata on the next evaluation. sanitize-reload runs the
same cases with ASan/UBSan, including generated native modules.

## Rest arguments

Functions accept dotted parameters, an ampersand followed by a final rest name,
or a single symbol for an all-rest parameter list. Function metadata records the
minimum arity and whether a rest slot follows the required parameters. Named
function definitions support the same dotted and ampersand forms. Optional parameters use the entry prologue described below.

Entry checks arity and reserves frame storage, then builds a fresh proper list
from extra arguments while the complete incoming argument range remains rooted.
Only after packing does a tail call move required arguments and reuse its frame.
The rest list receives its own local slot and is boxed when captured, just like
required parameters. Empty rest values use the existing empty-list immediate;
an unboxed empty-rest call performs no managed allocation. Fixed-arity native
self-call paths remain enabled only for fixed-arity functions.

Tests force moving collection during rest construction and exercise captured
rest lists, recursive calls, proper tail-space bounds, direct runtime_call,
freshness, duplicate/malformed parameter lists, too few arguments and recovery
from packing-time heap exhaustion. Ten thousand unboxed empty-rest calls leave
the collection count unchanged even with collection-at-every-allocation enabled.
The rest100000 benchmark calls a function with one required and two extra
arguments, summing its results in a loop. Python packs extras in a tuple; Otium
uses a list, and Chez may optimize away its rest list. All kernels use the same
arguments, result, warmup count and sample count.

The recorded rest kernel takes 3.540 ms native, 12.421 ms in the VM, 5.514 ms
in Python and 0.306 ms in Chez. Native Fibonacci remains 23.608 ms in the same
run. The rest-list implementation is correct but still allocates two pairs per
benchmark call; eliminating unnecessary rest allocation is future compiler work.

## Apply

apply accepts a callable, zero or more leading arguments, and a final argument
sequence. Proper lists and arrays are supported; nil means an empty sequence,
matching Chez. Expansion validates the sequence and stack limit before moving
any slots. It detects cyclic/improper lists, reserves host stack capacity, then
copies the callable and expanded arguments into the rooted VM stack. Expansion
itself performs no managed allocation.

The shared call helper handles closures, builtins, callable arrays/tables/keywords
and foreign bindings. Applying apply again expands in a loop rather than adding
C frames. Native call sites check the actual saved callee, publish roots and
resume through the same dispatch loop. A tail apply replaces its caller's VM
frame; a non-tail apply preserves the continuation. Global replacement during
argument evaluation does not change the already-selected callable.

Tests cover sequence forms, explicit leading arguments, nested apply, rest and
fixed arity targets, foreign calls, direct runtime_call, GC during the callee,
recursive calls, tail-frame bounds and recovery after malformed input or stack
exhaustion. Core differential coverage is 46 programs and 25 error cases; FFI
coverage is twelve programs and six errors. Chez's imported apply binding is
immutable, so the differential mutation test first defines a local binding.

The apply100000 warm benchmark forwards one leading argument and a reusable
sequence containing two more arguments to a fixed-arity function. Python uses
star expansion of a list, Otium applies an array, and Chez applies a list.
All kernels compute the same result with matching warmup/sample counts.

The recorded apply kernel takes 2.159 ms native, 10.318 ms in the VM,
7.422 ms in Python and 0.398 ms in Chez. Forwarding is about 3.4x faster
than Python in this kernel, while remaining about 5.4x slower than Chez.

## Optional parameters

The compiler accepts &optional followed by (name default) pairs. Required and
optional parameters occupy contiguous frame slots; local temporaries used by
default expressions follow those slots. Defaults compile as entry code guarded
by the internal PRESENT instruction, with omitted arguments represented by the
unbound immediate. Nil and false therefore remain supplied values.

Defaults execute left to right and can see preceding parameters. Each optional
name enters lexical scope after its own default expression, so that expression
can refer to an outer binding with the same name, matching Chez. Captured
parameters are boxed before the prologue runs, including omitted parameters;
default assignment updates the box. All argument-count checks occur before
executing defaults. Optional and rest parameters cannot be combined.

Entry accepts the required-to-total arity range, initializes omitted slots and
runs the same prologue in VM and native modes. Tail calls use the general frame
reuse path so omitted defaults are evaluated anew. Fixed-arity self-call fast
paths remain unchanged. Defaults require no argument-list allocation: ten
thousand calls using integer defaults trigger no collection with
collection-at-every-allocation enabled.

Tests cover partial/full argument lists, explicit nil/false, default order and
side effects, outer-name lookup, local temporaries in defaults, captured/default
closures under moving GC, tail and non-tail recursion, apply, direct runtime_call,
malformed declarations and arity errors before side effects. Core differential
coverage is now 53 programs and 32 error cases.

The optional100000 warm benchmark omits two integer defaults on each call.
Python uses literal default arguments; direct Chez uses an equivalent case-lambda
with the same arities and defaults. The result, warmups and samples match across
runtimes. General default expressions in Otium are evaluated at call time;
Python's arbitrary default expressions have different semantics, so this kernel
uses literals only.

The optional kernel records 2.355 ms native, 10.974 ms in the VM, 3.903 ms
in Python and 0.220 ms in direct Chez. Native is about 1.7x faster than Python
here, but roughly 11x slower than Chez. More call specialization is still needed.

## Multi-operand integer operations

Native arithmetic and comparison guards cover two through eight operands.
They check the saved callee and every operand's integer tag before entering
straight-line code. Three-to-eight-operand calls use a combined low-tag-bit
mask; binary calls retain their existing code generation. Addition, subtraction and multiplication check every
intermediate result for machine and tagged-integer overflow, preserving the
shared builtin's evaluation order. Chained comparisons operate on adjacent
operands. Mixed numeric types, larger argument counts and other callees retain
the generic implementation. The eight-operand limit bounds generated code size;
it does not limit supported call arity.

Checks cover both sides of that specialization boundary, intermediate overflow
even when a later operand would bring the result back into range, mixed
integer/float comparisons and arithmetic, invalid operands after an already
false comparison, and callee replacement during argument evaluation. Overflow
checks are prototype-specific because Chez supports larger exact integers.
The core Chez differential suite now contains 57 programs and 32 error cases.

comparison-nary.json records five alternating before/after runs (25 warm samples
per kernel), executable hashes and individual samples. Optional-call latency
falls from 2.234 ms to 1.808 ms, rest calls from 2.570 ms to 2.276 ms and apply
from 2.052 ms to 1.909 ms. Fibonacci stays effectively unchanged at about
22.2 ms. These are roughly 19%, 11% and 7% reductions in the three affected
kernels, rather than a uniform speedup across all code.

benchmark_compare.py repeats this comparison for supplied executables. Each
executable needs its matching internal headers, since native compilation uses
the configured header directory at runtime. Preserve a separate header directory
when comparing executables across internal ABI changes. The full cross-
runtime baseline remains in baseline-native.json. Broader compilation and
execution improvements are still needed to approach Chez consistently.

## Foreign metadata lifetime

After a moving collection, the runtime scans the surviving managed objects for
foreign binding and pointer wrappers. Dead bindings are freed. A live binding
keeps its signature adapter; an in-progress wrapper also pins its signature
across allocation. The signature cache retains at most four unused adapters in
binding-recency order, closing evicted modules. Runtimes without foreign
metadata skip this scan.

Freed allocation records survive as long as a pointer or view can refer to them,
so use-after-free checks do not dereference reclaimed records. Unreferenced,
released records are freed. Unreleased native buffers remain explicitly owned:
C may retain their addresses even after all managed wrappers disappear. Buffer
counts/bytes keep their existing meaning. RuntimeForeignStats additionally
reports library, binding, signature and allocation-record counts.

Repeated open-library calls that return the same OS loader handle balance the
extra loader reference and reuse the existing record. Managed library wrappers
remain distinct values. One loader reference per distinct library is retained
until runtime destruction. Automatic unload cannot safely follow wrapper
reachability alone because raw returned pointers and native retention can
outlive those wrappers. This policy bounds repeated reopening, but does not
bound the number of distinct libraries a program deliberately loads.

The collection hook also runs during allocation-triggered GC, not only between
evaluations, so explicit allocate/free churn inside a long-running function can
release its metadata. New wrappers have zeroed opaque payloads during allocation;
the scan ignores those incomplete payloads, and binding construction pins its
adapter until the wrapper is complete.

make -C prototype test-foreign-reload checks 300 VM and 30 eager-native reopen/
rebind cycles, signature-cache eviction, live bindings reachable only through
containers, 2,000 allocate/free iterations in one evaluation, released aliases,
and a buffer retained only by C. Observed counts stay at one library record,
two binding records and at most four unused adapters. ASan/UBSan tests include
both generated code and the C fixture through sanitize-foreign-reload.

comparison-foreign-collection.json records five alternating before/after runs
of allocation-heavy and foreign-call kernels. Pair allocation measured
0.964 ms before and 0.720 ms after; the million-call FFI loop stayed about
12.87 ms. These checks did not show a slowdown from the collection hook.
The cross-runtime timing files were refreshed separately; they remain single-
machine measurements rather than a general performance guarantee.

## Bounded direct native self calls

A direct self call retains the ordinary VM frame and exact stack roots. Before
calling the private generated body it saves the caller's continuation. If the
callee yields to another closure or reaches the direct nesting limit, a shared
dispatch driver runs only until that callee returns. It stops before executing
the suspended caller, which then reloads its stack pointer and resumes directly.
The exported module entry wraps a private static body, avoiding interposition
between modules that export the same entry name.

Native nesting is capped at 32 direct calls across the runtime, including calls
made through nested dispatch drivers. Arbitrarily deeper language recursion
uses VM frames. Proper tail calls keep their existing frame reuse. This bounds
C stack growth from language recursion; it does not establish a universal C
stack byte budget for generated functions or foreign code. Optional, rest and
boxed parameters retain the general call path.

The test-native-calls target executes 10,000 non-tail recursive calls with the
OS stack soft limit reduced to 256 KiB after compilation. It also checks VM
stack-limit errors and subsequent recovery, moving collection with managed
values retained by suspended callers, and mixed VM/native continuations. Both
host and generated modules pass ASan/UBSan through sanitize-native-calls.
runtime_max_native_depth and benchmark JSON expose the observed nesting limit.

comparison-direct-calls.json records five alternating before/after runs with
25 measured samples per kernel. Fibonacci falls from 22.412 ms to 16.828 ms,
about 25% less time. Seven other kernels stay within roughly 4% of their prior
medians. The saved pre-change executable uses a frozen copy of its matching
headers, since the runtime layout now includes native nesting counters.
The refreshed baseline measures Fibonacci at 16.823 ms native, 43.548 ms Python
and 2.859 ms Chez. This kernel is about 2.6 times faster than Python but remains
about 5.9 times slower than Chez; the broader performance goal remains open.

## Lexical scope roots

Non-tail let expressions and while bodies now clear their expired local slots.
The compiler emits OP_CLEAR_LOCAL for bindings owned by the scope, including
internal definitions and the initializer temporaries of named let. Nested
scopes clear their own bindings, avoiding repeated cleanup in enclosing scopes.
Tail-position lets need no cleanup instruction because return or tail-frame
reuse already removes those roots.

Clearing replaces the stack slot with UNBOUND. It does not mutate a captured
box: an escaping closure retains the box and its value through its own traced
fields. The expression result remains on the operand stack across cleanup.
The VM and generated native code implement the same instruction; the native
verifier checks its local index and unchanged operand height.

Previously, 32 sequential scopes building 300-element temporary lists retained
all those lists until the containing function returned. The workload exhausted
a 256 KiB semispace and first succeeded at 512 KiB among the tested powers of
two. It now succeeds at 16 KiB in VM and native modes. Both semispaces therefore
reserve 32 KiB for this example, versus 1 MiB at the previous successful setting.
This measures managed heap capacity, not total process memory or a general
minimum heap size. comparison-scope-memory.json contains the source and results.

The test-scope-roots target covers all three execution modes, collection at
every allocation, mutable escaping captures, per-iteration internal definitions,
returned managed values and named-let closures. sanitize-scope-roots instruments
the host and generated modules. Locals still in lexical scope are retained even
after their last use; full local liveness remains open. Lexical stack-slot reuse
is described below.

Five alternating before/after runs in comparison-scope-native.json and
comparison-scope-vm.json check eight existing kernels. VM Fibonacci stays at
about 141 ms and native Fibonacci at about 16.9 ms. The native rest-call kernel
initially measured 2.352 -> 2.588 ms, but a repeat measured 2.281 -> 2.258 ms.
Pair-allocation timing also varied across runs. The repeat is preserved in
comparison-scope-native-repeat.json; these measurements do not establish a
consistent speed improvement. The demonstrated benefit is reduced retention
of expired locals.

## Stack slot reuse

Bindings keep logical local indices while their function is compiled. The
compiler records where each binding starts and where its lexical scope ends.
After nested functions finish capture analysis, it assigns physical slots to
non-overlapping lifetimes. Two priority queues order available slots by lifetime
end, separating boxed and unboxed bindings. Parameters retain their original
positions for call entry, rest packing and optional defaults.

The pass rewrites local bytecode operands and the local capture indices of each
immediate child closure. Captures through an enclosing closure remain unchanged.
A fresh boxed binding still allocates a fresh box, so escaped closures keep their
old values when a later binding reuses the slot. Separating boxing policies also
prevents an unrelated unboxed binding from acquiring heap allocation merely
because it shares storage with a captured binding.

The function retains only its physical local count and compacted boxing flags.
Compiler lifetime records, slot queues and the logical-to-physical map are freed
after compilation. Assignment uses O(L log L) time and O(L) temporary storage for
L logical locals; runtime dispatch needs no mapping lookup. This uses lexical
lifetimes, not full last-use analysis inside a scope.

The test-slot-reuse target compiles 256 sequential local scopes to one physical
slot and runs with a 32-slot VM stack. The previous executable exhausted stack
limits through 256 slots and first succeeded at 512 among the tested sizes.
comparison-slot-reuse.json records the source, executable hashes and outcomes in
both VM and native modes. Additional tests retain independent mutable closures
from reused slots, exercise transitive captures, nested shadowing, loop-local
definitions and optional/rest parameters under moving collection at every
allocation. VM, eager-native and tiered modes pass, including ASan/UBSan for both
host and generated modules through sanitize-slot-reuse.

comparison-slot-native.json records five alternating before/after runs of eight
existing kernels. Fibonacci measures 16.853 -> 16.637 ms and rest calls remain
about 2.39 ms. These kernels do not directly stress sequential lexical scopes;
the comparison checks for regressions rather than establishing the speed benefit
of slot reuse. The demonstrated improvement is the smaller frame requirement.

## Immediate integer decoding

On targets with two's-complement intptr_t and arithmetic right shift, decoding
now copies the tagged representation into intptr_t and shifts it right once.
The bit copy avoids an out-of-range unsigned-to-signed conversion. Compile-time
checks select this path; other targets retain the previous portable expression.
OTIUM_PORTABLE_INTS forces that fallback for verification. Encoding, value
range, tags and arithmetic overflow behavior remain unchanged.

GCC 15.2 and Clang 21.1 on this x86-64 host emit a register move and one arithmetic
shift for the new decoder. GCC emitted eight instructions for the previous
sign-handling expression. The shared inline helper is used by the VM, generated native code,
collections and foreign argument conversion.

The test-value target checks range boundaries, every integer from -65536 through
65536, values around powers of two, and two million positive/negative randomized
round-trips. A volatile encoded value prevents the compiler from eliminating
round-trips by proving the composition algebraically. Both paths pass with GCC,
Clang and ASan/UBSan; sanitizer coverage also includes the runtime and generated
native modules. These tests exercise the fallback on the current host, not an
actual non-two's-complement machine.

Native three-through-eight-operand integer arithmetic also keeps intermediate
results tagged. With T(x) = 2x + 1, addition computes T(x) + (T(y) - 1),
subtraction computes T(x) - (T(y) - 1), and multiplication computes
(T(x) - 1) * y before restoring the tag. Checked signed overflow exactly matches
the fixnum range on the selected representation. Every intermediate operation
is checked, including when a later operand would bring the result back in range.
The portable helpers decode and explicitly check the fixnum bounds instead.

The primitive tests additionally compare a million operand pairs against
independent division/boundary-based arithmetic checks, plus all combinations of
minimum/maximum fixnums, adjacent limits, zero and small signed operands. The
existing runtime tests cover mixed floats, invalid types, intermediate overflow
and live replacement of the arithmetic callee.

Binary arithmetic and comparisons retain their existing generated forms. An
experiment using tagged arithmetic and ordering throughout slowed sum1000000
from 6.492 to 8.794 ms. Restoring the binary forms brought the final paired
measurement to 6.476 -> 6.446 ms. The experiment and selective variants are
preserved in comparison-tagged-*.json; the final nine-kernel comparison is in
comparison-integer-final.json.

The final comparison measures Fibonacci 16.968 -> 16.572 ms, rest calls
2.362 -> 2.260 ms and apply 1.868 -> 1.815 ms. Results vary by run: a table
outlier of 0.343 -> 0.460 ms repeated at 0.464 -> 0.461 ms, and pair allocation
repeated at 0.951 -> 0.987 ms. comparison-integer-final-repeat.json retains those
checks. These are small, workload-dependent changes, not a broad speedup or
resolution of the gap to Chez. Decode-only VM/native trials are also retained
in comparison-int-decode-*.json.

## Native loops with private frames

Eligible integer loops now execute against a private C array rather than writing
intermediate values through the runtime stack. Fixed offsets let the host C
compiler keep loop values in registers. Values retain their ordinary tags; this
is not a typed SSA backend. The ordinary native body remains in the same module
for unsupported inputs and continuations.

Eligibility requires unboxed locals, fixed arity, at most 4,096 bytecode
instructions and at most 128 combined closure/local/operand slots. The function
must contain a backward jump or a potential self tail call. Its reachable
instructions may read globals or captures, change locals, branch and perform
checked integer arithmetic/comparisons. Allocation, managed constants, arbitrary
non-tail calls, shared-state mutation, rest/optional parameters and other
operations exclude the function. The private array adds at most 1 KiB on a
64-bit host, plus compiler-generated stack storage.

Entry requires integer parameters and checks that referenced arithmetic globals
still hold the expected builtins. The private copy leaves the original VM value
frame unchanged. Arithmetic validates its operands; a potential self tail call
checks closure identity and integer arguments before reusing the private frame.
Captured numeric values are read from their current boxes, so updates between
calls remain visible. There are no allocation or GC safepoints in this path.
The current runtime is synchronous; this proof depends on excluding arbitrary
calls and nonlocal mutation throughout the private body.

If an in-body check fails, execution discards the private copy and resumes the
ordinary native body at the saved entry state. Earlier work may be repeated, but
it has no externally visible effects to duplicate. Overflow uses the existing
error path. A successful return publishes its result through ordinary rt_leave.
Initial guard failures skip the private body entirely. The scalar_entries and
scalar_fallbacks diagnostics count attempted private execution and in-body
replays, respectively; they do not count loop iterations.

Straight-line arithmetic helpers keep the ordinary native path. Applying the
private copy to those helpers added roughly 10% to the pinned apply benchmark,
so the final eligibility check requires a loop. An earlier broad variant is
recorded in comparison-native-scalar-pinned.json. benchmark_compare.py now
accepts --cpu to pin itself and its subprocesses, and reports CPU affinity.
The cross-runtime scripts also record inherited affinity. Pinning reduces
migration between CPUs; it does not eliminate timing variation.

The final tests cover eager and tiered entry, named self tail loops, floats at
entry and after many iterations, a late tail transfer to a side-effecting
function, builtin replacement, saved closures after redefinition, captured-box
updates, overflow/uninitialized-local errors and recovery. Twenty-four generated
branching arithmetic loops match the VM and assert that private execution was
entered. The focused tests pass with GCC and Clang-generated modules, and with
ASan/UBSan instrumenting both host and generated code. Core runtime, differential,
reload, FFI and existing root/stack tests also pass.

comparison-native-loops-pinned.json records the final five alternating runs on
CPU 0, 25 measured samples per kernel. sum1000000 falls from 6.441 to 0.885 ms,
a 7.3x improvement. The other eight kernels stay within about 2.3% of their prior
medians. The final comparison excludes the straight-line helper experiment.
The refreshed cross-runtime baselines also use CPU 0: the sum loop measures
0.900 ms native, 18.230 ms Python and 0.575 ms Chez. Arrays measure 0.356 ms
native and 0.345 ms Chez, but Fibonacci remains 16.654 ms versus Chez's 2.869 ms.
This improves one important class of loops; it does not establish comparable
performance across the language.

## Floating-point and LuaJIT reference measurements

The reference suite now includes floating-point accumulation and tables keyed
by object identity. These expose work that the integer-loop optimization does
not handle. The floating-point case adds 0.5 one million times and checks that
Otium returns a boxed float containing exactly 500000.0. The object-key case
allocates 5,000 distinct keys, inserts twice each index, then retrieves every
value through a separately grown key array. Keys are empty Otium arrays, Lua
tables, Python objects and mutable Chez vectors. Chez's auxiliary vector grows
from four slots by doubling. These measure allocation and collection access
together; differences in object representation remain part of the measurement.

The installed LuaJIT shared library can run the references without a Lua command
or development headers. bench_lua.c loads its public Lua 5.1 C API and supplies
a CLOCK_MONOTONIC callback used at the boundaries of each measured call. LuaJIT
[documents Lua 5.1 ABI compatibility](https://luajit.org/extensions.html), and
the declarations follow the [Lua API manual](https://www.lua.org/manual/5.1/manual.html).
OTIUM_LUAJIT_LIBRARY overrides automatic library discovery. Missing libraries
skip these optional references. Ruby and standalone Lua remain unmeasured.

Each Lua workload runs in a fresh process with the JIT explicitly enabled or
disabled through the [jit module](https://luajit.org/ext_jit.html). The output
labels distinguish LuaJIT's interpreter from its JIT-enabled mode. Enabling the
JIT does not establish that all executed code compiled. Version, architecture,
mode and trace counts are recorded. Dense integer keys can use Lua's array
storage, so tables10000 does not isolate hash-table performance; objectkeys5000
adds an identity-key workload.

Lua measurement requires at least two warmup calls and five measured calls.
Starting a trace during a call discards that call and earlier samples; collecting
five samples resumes afterward. A limit of 100 calls catches failure to settle.
Every call checks its result. Samples are never discarded for being slow.
Actual warmup and discarded-sample counts are recorded. The Fibonacci reference
needed more than two warmups in the saved full run. A focused harness test
forces compilation after sampling begins and verifies that earlier samples are
discarded. Missing libraries, malformed scripts, string/non-string errors and
wrong answers are also checked. The C runner passed an ASan/UBSan smoke test;
the system LuaJIT library itself was not instrumented.

baseline-native.json contains ten workloads across six runtimes;
baseline-foreign.json adds the LuaJIT FFI references. All calls returned the
expected results. baseline-expanded-repeats.json preserves three additional
runs on CPU 0 with alternating workload order. Each run has five samples per
runtime/workload. Medians across the fifteen samples, in milliseconds:

| Workload | Native Otium | Python | LuaJIT interpreter | LuaJIT enabled | Chez |
| --- | ---: | ---: | ---: | ---: | ---: |
| float1000000 | 10.272 | 14.794 | 3.040 | 0.504 | 2.461 |
| objectkeys5000 | 0.303 | 0.340 | 0.399 | 0.354 | 0.203 |
| sum1000000 | 0.924 | 18.131 | 3.056 | 0.523 | 0.571 |

Timing variation remains visible in the artifacts. The full-suite run measured
native floating-point accumulation at 19.111 ms; the three targeted run medians
were 10.272, 10.201 and 10.310 ms. The reason for that difference has not been
isolated. Both sets show a substantial floating-point gap. The ordinary native
path still boxes arithmetic results, and the private integer path never enters
for this workload. Unboxed floating-point loops are the next performance target.
The FFI run measured 12.736 ms native, 144.447 ms ctypes, 35.269 ms LuaJIT
interpreter, 2.374 ms LuaJIT enabled and 2.415 ms Chez.

Reproduce the comparisons with:

```sh
make -C prototype bench_runtime bench_lua test-benchmark-lua
python3 prototype/benchmark_steady.py --cpu 0
python3 prototype/benchmark_steady.py --cpu 0 --workloads float1000000 objectkeys5000 sum1000000
taskset -c 0 python3 prototype/benchmark_foreign.py
```

This work changes benchmark coverage and measurement, with no runtime/compiler
behavior changes. The broad language and performance goals remain open.

## Unboxed floating-point loops

A second private execution path now handles numeric loops with floating-point
constants, parameters, globals and captures. It supports the same two-to-eight
argument arithmetic and comparisons as the integer path. Division, arbitrary
calls, shared mutation, boxed locals and rest/optional arguments still use the
ordinary runtime. Eligibility retains the 4,096-instruction and 128-slot bounds.
The integer path runs first where applicable.

Each numeric temporary has a value, double and numeric kind. Reads preserve the
original value, including the identity of a boxed float. Arithmetic produces an
unboxed double with a zero value marker; zero is not a language value. Integer
operations retain checked fixnum arithmetic. Mixed comparisons compare integers
against doubles without first rounding the integer. Addition and multiplication
include their identity operand, matching the VM's operation order and signed
zero behavior. Generated modules disable floating-point contraction. Hosts with
FLT_EVAL_METHOD other than zero skip this path and retain ordinary execution.

The emitter gives each temporary a distinct C variable and inlines arithmetic
helpers. Keeping the temporaries in an indexed array prevented the host compiler
from keeping them in registers and erased the expected speedup. The numeric
body is a separate, non-inlined function: merging it into the existing native
body slowed the integer sum loop from 0.929 to 1.330 ms. The separate body
restored 0.929 ms. comparison-native-numeric-inline.json preserves the merged
variant, and comparison-native-numeric-separated.json records the targeted check.
The temporary representation occupies at most 3 KiB on this 64-bit host before
host-compiler optimization, plus compiler-generated storage. Self tail calls
reuse these variables without growing the C stack.

No allocation occurs while private values are live. The unchanged VM frame,
globals and constants retain the original objects as precise GC roots. A return
of a computed float allocates one box, then calls rt_leave without reading any
private pointer afterward. Returning a loaded value allocates nothing. Failed
callee/type checks replay the original frame through the ordinary path, with no
shared effects to duplicate. An integer-path replay may first try the numeric
path. The existing scalar counters count both paths; numeric_entries and
numeric_fallbacks in benchmark output identify the numeric subset.

The new tests compare 21-by-21 numeric input combinations across eight operators
against the VM, at eager and tiered thresholds. They cover signed zero,
subnormals, infinities, NaNs, large integers, intermediate overflow before a later
float operand, n-ary arithmetic, separate multiply/add rounding, self tail calls,
late type errors and recovery, retained object identity, mutable captures, live
builtin replacement and a tail transfer to a side-effecting function. GC stress
runs throughout. A separate assertion verifies exactly one collection for a
warmed-up 100-iteration float loop when every allocation forces collection.

The focused tests pass with GCC and Clang-generated code and with ASan/UBSan
instrumenting the host and generated modules. LeakSanitizer cannot run under the
environment's tracing setup; sanitizer runs use ASAN_OPTIONS=detect_leaks=0.
Core VM/eager/tiered tests, bounded native calls, scope roots, slot reuse, reload,
FFI/reload, 57 Chez differential programs with 32 errors, and 12 FFI differential
programs with six errors also pass. Optional LLVM development tools are absent.

comparison-native-numeric-pinned.json records five alternating before/after runs
on CPU 0, 25 samples per workload. Floating-point accumulation improves from
10.294 to 0.802 ms, about 12.8x. Integer accumulation stays at 0.929 ms. Other
workloads range from about 1.3% faster to 3.7% slower in this run; this change
does not resolve their performance gaps. The baseline float timings still show
the previously observed 10–19 ms variation, preserved in the raw samples.

A fresh six-runtime comparison in baseline-native-numeric.json measures floats
at 0.793 ms native, 13.821 ms Python, 3.017 ms LuaJIT interpreter, 0.472 ms LuaJIT
enabled and 2.403 ms Chez. Integer accumulation remains 0.924 ms native versus
0.476 ms LuaJIT enabled and 0.574 ms Chez. These results establish a substantial
improvement for pure numeric loops. Recursive calls, broader language support,
Ruby measurements and the remaining performance gaps are still open.

## Reload-preserving definitions

Top-level `defonce` now initializes a user binding only on its first successful
definition. Existing values, including nil and false, survive later evaluations.
An existing binding skips initializer compilation as well as execution, so a
reload cannot introduce side effects, allocate replacement state, or fail because
of invalid code inside an unused initializer. The outer form still requires a
symbol name and the expected operand count. An optional docstring follows the
existing `def` parser; reflection and documentation storage remain unimplemented.
`defonce` returns nil and is rejected in function bodies, local scopes and other
expression positions. Function shorthand is not accepted; use a function value
as the initializer.

One byte of metadata per global distinguishes user definitions from the initial
builtin environment. Both VM and generated native definition stores mark the
binding only after its initializer succeeds. A failed initializer can be retried.
Ordinary `def` also marks the binding, so later `defonce` preserves it. A first
user `defonce` may shadow a builtin; this matches the primary implementation's
local-definition behavior within the prototype's current flat global environment.
Namespace resolution and module loading are described below.

Top-level `begin` now evaluates its children in order, compiling each after the
preceding child has run. This makes declarations visible to later `defonce`
forms, including nested begins. Earlier successful definitions survive a later
compile or runtime error. Function-body begins keep their existing lexical-body
compilation. Empty begins still return nil. Each top-level child uses the same
code/constant reclamation boundary as a separate source form.

Tests cover saved mutable state shared by old and replacement callbacks, builtin
shadowing, nil/false values, ordinary definitions, documentation syntax, failed
initializers and compilation recovery, invalid contexts, nested begins and
1,000 repeated reloads. The repeated skips create no new native compilations
and keep code/constant slot counts bounded. These tests pass under GC stress in
VM, eager-native and tiered modes, and with host/generated ASan/UBSan (leak
detection disabled for the environment limitation described above). The public
Chez comparisons now cover 65 programs and 42 errors, including eight defonce
programs and ten invalid uses.

## Namespace-bound globals

`ns` now selects a persistent namespace for subsequent top-level forms. New
runtimes start in `user`; ordinary builtins belong to `otium.core`, and FFI
bindings, including the null pointer, belong to `ffi`. Namespace names and each
global's namespace ID live in host metadata. The values remain in the existing
precisely rooted global array. Namespace ownership adds four bytes per global,
plus one stored name per namespace and the registry's capacity overhead.

Definitions create a binding in the selected namespace. References first check
that namespace, then a qualified reference to a known namespace, then the core
bindings. An unresolved name reserves a slot in the selected namespace, allowing
forward references. A literal local name containing a slash takes precedence
over qualification, matching the primary implementation. Namespace IDs and local
names are stored separately, so the registry does not conflate them through
string concatenation. `ns` is top-level only; its require clauses use the module
loader described below.

Compiled functions retain resolved global indices. Changing the current
namespace cannot redirect an existing caller. Redefining the referenced binding
updates existing callers; retained function values keep their old code. Qualified
assignment updates the selected binding. Each namespace has independent defonce
state. The host runtime_call API resolves unqualified names in the current
namespace and also accepts qualified names; a failed lookup does not create a
new global.

This fixes a difference in the earlier flat environment: a later local definition
that shadows an imported builtin must not redirect callers already compiled
against the core binding. Conversely, callers compiled against an existing local
binding must see its replacement. The native replacement tests now establish a
local arithmetic binding before compiling those callers, and namespace tests
check both behaviors. Replacing the core binding itself still affects callers
that reference it. All native builtin shortcuts retain their callee-value guards.
Namespace lookup occurs during compilation or host lookup, without an additional
lookup in the hot VM/native call path.

Tests cover namespace switches, independent globals and defonce state, qualified
reads/writes, local and qualified forward references, lexical captures, old and
replacement callbacks, core/local shadowing, core redefinition, FFI access from
inside and outside its namespace, invalid declaration contexts, and 1,000
switch/reload cycles with fixed namespace and global counts. They run under GC
stress in VM, eager-native and tiered modes. The Chez differential suite now
contains 74 programs and 50 errors. The focused tests also pass with Clang-generated
modules and host/generated ASan/UBSan; leak detection remains disabled for the
environment limitation described above.

The module loader below extends direct namespace resolution. Namespace reflection
and macro expansion remain open; the public library cannot yet be loaded in full.

baseline-namespaces.json records a fresh CPU-0 comparison after the namespace
change. Native float accumulation measures 0.791 ms, integer accumulation
0.911 ms and Fibonacci 17.098 ms. Chez measures 2.442, 0.600 and 2.841 ms
respectively; LuaJIT enabled measures 0.511, 0.557 and 4.678 ms. These are fresh
reference measurements, not a paired attribution of timing changes to namespace
support. The recursive-call gap remains substantial.

## Module loading and imports

Top-level `require` now loads Otium source modules. The CLI accepts repeated
`-L DIR` options, and embedding applications can call runtime_load_path. Search
visits those directories in order, then `.` and `lib`; within each directory it
tries `.ot` before `.scm`. Dots in module names become directory separators. The
public choice/suffix fixtures verify that an earlier directory's `.scm` wins over
a later directory's `.ot`. Scheme-suffixed files still use the prototype's Otium
reader/compiler, as those fixtures do.

Each namespace has an unloaded, loading or loaded module state. Successful loads
are cached. `:reload` reevaluates the file, preserving existing globals and
`defonce` state. Failed initial loads and failed reloads become retryable, while
effects from forms already evaluated remain visible. Deleted definitions also
remain. Circular requires fail without leaving modules stuck in the loading
state. The loader restores the caller's namespace and its enclosing module's
namespace constraint on both success and failure. Explicit ns declarations must
match the requested module. A file without ns inherits the requested namespace,
matching direct checks against Chez.

Requires accept flat, grouped and quoted specifications, several modules in one
form, `:as`, `:refer`, and `:reload`. Empty require is a no-op. Namespace declarations
can contain grouped `:require` clauses. Aliases and referred bindings belong to
the importing namespace and resolve during compilation to existing global slots.
Repeated imports update the mapping without allocating another entry. Retargeting
an import affects later compilation; existing callers retain their earlier
resolved slots. Aliases cannot collide with a different namespace's name, but
an alias equal to its own target namespace is accepted.

A referred import must name a public binding defined by the requested module.
Definitions and explicit referred imports cannot share a local name. Successful
top-level `def-` stores mark a binding private; ordinary definitions clear that
mark. Direct qualified access remains available, as in the primary runtime.
Qualified lookup can also follow an imported binding in the target namespace,
but that inherited binding cannot be imported again through `:refer`.

One primary-runtime limitation surfaced during differential testing: a qualified
value inherited through a referred import can be saved and called, but Chez
rejects using that value directly as a call head with a top-level-value error.
The prototype accepts both forms. The differential test uses the saved value;
the focused tests also cover direct calls. This difference is retained explicitly
rather than treating a primary implementation error as a language requirement.

Private visibility adds one byte per global. Module state contains no managed pointers. Global values and functions keep the
existing precise roots and code-reclamation rules. Requires execute only at
top-level boundaries, where no VM frame is suspended. Recursive loads keep array
indices across calls, since dependency loading can grow the module/import arrays.
Active module sources share a 16 MiB budget, and nesting is capped at 128 loads.
Both limits fail with an error and leave the loader usable. Namespace/import
metadata persists until runtime destruction; repeated loads of the same names
reuse entries. Cached requires skip file evaluation and native compilation but
still use the ordinary top-level reclamation boundary.

The public retry fixture uses `when`, so the compiler now also lowers `when` and
`unless` directly to conditional branches. They preserve Otium truthiness, return
nil on the skipped branch, return the selected body's value and retain tail-call
position. Definitions in those expression bodies are rejected. General macro
expansion remains unimplemented.

Focused tests exercise public search/suffix, retry, mismatch and cycle fixtures,
aliases/refers, private/conflicting names, caller restoration, cached loads,
retargeted imports, failed reload effects, deleted definitions, old versus live
callbacks, deep dependency rejection, oversized sources and recovery. They pass
under GC stress in VM/eager/tiered modes, with GCC and Clang-generated modules,
and with host/generated ASan/UBSan (leak detection disabled for the known
environment limitation). A separate Chez differential suite passes 15 programs
and 18 expected errors. Existing runtime, namespace, defonce, native numeric,
reload, FFI and root-lifetime checks also pass.

`otium.core` and `ffi` are treated as already available builtin modules; requiring
them does not load the higher-level standard library. Macro expansion, syntax
quotation, condition handling, reflection and additional library operations
remain necessary before the full public module test harness or standard library
can run. Native compilation still has no per-call namespace or import lookup.

## Quasiquotation

Backquote now supports unquote, splicing, dotted tails and nested quasiquotes.
The compiler builds a small plan before emitting bytecode. Static subtrees become
managed constants, preserving sharing between calls; dynamic portions allocate
fresh list structure. Active unquote expressions run from left to right. Plain
quasiquotation keeps symbols unqualified. As in the primary reader, bracket and
brace forms inside a template are constructor data such as `(array ...)` and
`(table ...)`, rather than runtime collections.

Nested quasiquote increases depth; unquote and splicing decrease it. Only active
unquotes evaluate expressions. A list tail has the same interpretation whether
written with a dot or ordinary list notation: `(a unquote x)` inside backquote
has the behavior of `(a . ,x)`. The compiler follows the primary runtime's marker
handling, including retaining inactive markers and using their first operand.
Unquote outside quasiquote and markers without operands are rejected.

A final splice supplies the template's tail directly. It can therefore return
nil, an array, an improper list or another arbitrary value, and preserves that
value's identity. A middle splice must be a proper, acyclic list. Its spine is
copied after the remaining expressions have been evaluated, and the final tail
is shared. This order matters when a later unquote mutates an earlier splice
value. Cyclic middle splices fail instead of looping indefinitely.

Constructors are compiler-supplied builtin constants, so replacing user or core
bindings named cons or append cannot change template construction. A private
splice builtin validates and copies the prefix without invoking user code.
It is not installed in the language's global environment. Native call analysis
recognizes these builtin constants, allowing the existing guarded cons fast path
to handle generated construction calls. An entire template consisting of an
active unquote or final splice retains tail-call position.

The plan stores AST pointers and node indices in host memory; every managed
constant is held in a separate precise root array. Reallocation updates that
root array before another managed allocation. Constant folding reloads its child
values after allocation. Planning and emission iterate over flat lists using
explicit work arrays, so template length does not consume C stack. Nesting still
uses the reader's existing depth bound. Plan payload is limited to 16 MiB, with
additional capacity/allocator overhead. Dynamic flat templates can still require
VM operand stack proportional to their length. The splice helper roots its
source, tail and partially copied result through every allocation.

Tests pass under GC stress in VM/eager/tiered modes, including nested depth,
static sharing, copied spines, arbitrary final tails, evaluation order, mutation,
shadowed constructor names, cyclic/improper splice rejection, tail recursion and
allocation-failure recovery. An 8,000-element dynamic template also runs with a
256 KiB C stack. Compile-time heap exhaustion and exhaustion during splice copying
both leave the runtime usable. GCC/Clang-generated modules and host/generated
ASan/UBSan pass; leak detection remains disabled for the environment limitation
noted above. The new Chez differential suite passes 38 programs and 11 errors.
Existing native, root-lifetime, reload, module and FFI checks also pass.

This supplies data templates for future macros. It does not implement defmacro,
syntax quotation or hygienic expansion, and the higher-level standard library
still needs those features and condition handling.


## VM-first compliance work

The current objective is to run Otium's core language and public tests, use
libffi, and temporarily remove generated C execution. Earlier native-tier
measurements in this document are historical results, not the new acceptance
criteria.

`python3 prototype/compliance.py --output prototype/compliance-baseline.json`
runs the existing public test functions for the language, library, modules,
reader, conditions, and syntax quotation against the prototype executable.
Assertions remain in the public harness. The initial run reports 3 passes and
46 failures; most programs cannot yet load the test library, and some fail on
missing launcher options. This inventory also includes higher-level consumers
from tests/tests.py. It does not cover all FFI, CLI, diagnostics, application,
or Chez-only tests and is not by itself a completion gate for the full goal.

The libffi migration passes scalar/ABI, ownership, reload and failure-recovery
tests, 12 differential programs and 6 expected errors, and ASan/UBSan with leak
detection disabled for the existing tracing limitation. The VM foreign-call
suite also passes with PATH and OTIUM_NATIVE_CC pointing to nonexistent paths.
Callbacks and reentry remain unimplemented. Core expansion and library bootstrapping are the next steps.


### Generated-C removal

Removed the generator, numeric specialization header, compiler subprocess/DSO
helper, function heat counters, native entry points, CLI flags and compile-time
statistics. The build no longer exports executable symbols for generated DSOs.
The bytecode compiler and VM remain. Differential drivers now compare the VM
with Chez; current benchmark drivers measure VM execution. Historical JSON
measurements remain as records of the earlier implementation.

Semantic tests for closures, arithmetic, calls, roots, reloads, namespaces,
modules and quotation remain. Tests specific to generated-code specialization
were removed with that backend. VM regression tests and all four differential
suites pass (74/50 language programs/errors, 15/18 module, 12/6 FFI, 38/11
quasiquote). Runtime and FFI/reload ASan/UBSan checks pass with leak detection
disabled as above. `make -C prototype test-no-compiler` exercises runtime and
foreign-call suites with no executable search path. The resulting executable
imports ffi_call and ffi_prep_cif and has no process-launch imports.

This completes removal of runtime C generation, not core language compliance.
The initial public-suite inventory still exposes expansion, core bootstrap,
conditions, reflection, I/O and launcher work.

### Initial macro expansion

Top-level defmacro now creates a VM closure marked as an expansion binding.
Calls pass unevaluated argument data through the VM and copy the result into a
bounded host syntax arena. Managed argument values stay in precise VM roots;
the resulting syntax holds copied text and scalar data, never managed pointers.
List spines are traversed iteratively with cycle detection. Nested syntax and
expansion counts are bounded, and failed expansions leave evaluation retryable.

Expansion respects lexical shadowing, including enclosing closures and
sequential let bindings. Namespace-qualified macros use existing binding lookup.
Redefining a macro affects subsequent compilation; already compiled functions
retain their expansion. An ordinary def clears the macro marker. Macro bodies
may be empty, matching Chez. Quasiquote-based variadic macros, nested macro
calls and top-level generated definitions work.

The focused forced-collection suite covers expansion, shadowing, reloads,
malformed forms, cyclic output, expansion limits and error recovery. ASan/UBSan
passes with leak detection disabled. Twelve programs and three expected errors
agree with Chez. This is an initial expansion layer: body predeclaration for
macro-generated local definitions, expansion APIs, hygienic symbols and syntax
quotation remain unfinished. Macro-produced top-level directives also still
need integration with the top-level evaluator. Core/test-library bootstrapping
and the broader public-suite goal remain incomplete.

### Shared Otium expander

The CLI now loads lib/otium/expander.ot into otium.core and applies its expansion
rules before bytecode compilation. runtime_create remains a minimal VM API;
embedders opt into the shared layer with runtime_load_expander. The build records
the library directory for the CLI. Relocatable library discovery is still open.
The original C implementation has not been used.

The runtime supplies the documented expander adapters, a nested VM call path
that keeps suspended stack frames rooted, cond, basic symbol/type/string
operations, and proper-list length. macroexpand and macroexpand-1 are small
Otium wrappers around the shared expander. Module directives retain their
existing validation before normalization; defonce skips an already-defined
initializer before expansion. Compile-time macro calls have a 1,024-call limit.

Generated symbols carry an identity separate from their printed text. The
syntax arena preserves that identity, and lexical binding comparisons include
it. Converting a generated symbol's name back into an ordinary interned symbol
does not reproduce the generated binding. Syntax quotation and local body
handling now come from the shared language implementation, rather than a
second implementation of those rules in C.

Forced-collection tests cover generated local definitions, spliced begin forms,
nil after a trailing definition, nested VM macro calls, reserved-name errors,
generated-symbol capture avoidance, Unicode string operations, expansion APIs,
and recovery after expansion limits. The shared-expander suite passes
ASan/UBSan with leak detection disabled. Eighteen expansion programs and three
expected errors agree with Chez; existing language, module, quasiquote and FFI
differential checks also pass.

The public array-equality suite now passes all 17 checks. compliance-current.json
records 4 passes and 45 failures, compared with the initial 3/46 baseline.
The full core library is not yet loaded: derived collection operations,
conditions, classes/generics, ports and other library dependencies remain open.
The raw compiler expansion layer remains for minimal VM users; the CLI uses
the shared expander for full syntax processing.

### Condition values and VM try frames

The runtime now registers condition type parents, checks hierarchy membership,
constructs error tables with optional data arrays, and exposes condition-type,
condition-message, error? and type-pred. Registrations accept symbol, keyword or
string names and reject hierarchy cycles. error? follows the documented broad
convention: any table containing :type qualifies, regardless of ancestry.

try uses VM handler records containing frame count, stack height and catch
instruction offset. An error unwinds to that saved VM state; the catch path
then evaluates predicates in order and binds the original condition value.
Predicates see their enclosing lexical scope, and errors raised in a catch
propagate to an outer handler. Unmatched conditions are rethrown unchanged.
Ordinary runtime errors become condition tables when caught. Suspended nested
VM calls preserve the surrounding handler stack as well as value roots.

A runtime root keeps the pending condition alive, including nil and false,
and another keeps the type registry alive. Explicit error strings now appear
in host diagnostics. Strings nested in printed collections are quoted and
escaped, fixing a mismatch exposed by the condition differential tests.

The focused forced-collection suite and ASan/UBSan checks pass. Ten thousand
nested protected calls unwind on a 256 KiB C stack. Seventeen condition programs
and three expected errors agree with Chez, and the existing language differential
suite still passes 74 programs and 50 expected errors. Runtime, shared-expander,
macro, module, quasiquote and compiler-free FFI regression checks pass.

This is not the complete condition system. Signal-site handler-bind, restarts,
unwind-protect, interrupts and exit transfers remain to be implemented. Loading
the core source now reaches defgeneric after completing its condition declarations;
generic dispatch and built-in class descriptors are the next bootstrap dependency.
The public-suite inventory remains 4 passing and 45 failing.

### Core bootstrap and primary generic methods

The CLI now evaluates the complete shared core library in otium.core, including
its otium.loop dependency. runtime_load_core exposes this bootstrap to embedders;
runtime_create remains minimal. A small prototype/primitives.ot supplies derived
operations such as for-each, reverse, append, inc/dec and collection assignment
in Otium. Library and bootstrap file paths are currently recorded at build time.

Built-in class descriptors have stable managed identities and an Object parent.
Primary generic methods use fixed-arity class signatures. Dispatch selects a
unique undominated applicable signature and reports missing or ambiguous
methods. Redeclaration preserves a compatible generic's identity; replacing a
signature replaces its closure. Method introspection copies the returned lists.
The current selector scans methods without a dispatch cache.

Macro values now have managed wrappers, so ordinary definitions can re-export a
macro without turning it into a runtime function. This is needed by the core's
for/dotimes aliases. read-string now parses exactly one datum through the
prototype reader. Basic display/write/newline operations and closure-arity
condition code E0301 support the public test library.

Bootstrap also exposed two namespace bugs: unbound placeholders in core must
not count as imports into other namespaces, and defonce must test its own
namespace before skipping expansion. Regression tests cover both cases.

Primary generics and core bootstrap pass forced-collection and ASan/UBSan tests,
including 200 method replacements. Ten generic/sequence programs and four
expected errors agree with Chez. The existing language, macro, module,
quasiquote and FFI differential suites pass. The CLI loads the full core and
executes a derived operation with no compiler on PATH.

The public optional-parameter suite reports 52 checks passed; the inventory is
now 5 passing and 44 failing. This does not prove the full optional/evaluation
surface: eval is still absent, so tests that only expect an error from eval are
weak evidence until that primitive is implemented. User-defined classes,
instances and migration, around/next methods, remaining conditions and ports,
reflection, numeric/string operations and launcher features remain unfinished.

### Runtime evaluation and namespace capture

`eval` now expands and executes runtime data without discarding the suspended
VM stack or its condition handlers. Calls from macros, nested eval, module
loading through eval, and errors during compilation share this path. Each eval
value retains the namespace where it was obtained, matching Chez when an
evaluator is saved or passed to a function in another namespace. Lexical locals
are not visible to evaluated code.

Managed arrays, tables and procedures in generated syntax are retained through
constant-table indices in the host AST. They evaluate to the original value.
This also lets macros return opaque values. The shared expander's diagnostic
codes now survive conversion to condition tables.

Code and constants are reclaimed at an idle outer evaluation boundary. Failed
compilation no longer deletes constants or closures that a macro's side effects
retained. Expanded begin forms keep their opaque constants alive across child
forms. Nested eval is limited to 128 active evaluations to bound host recursion.
A long-running evaluation can still accumulate temporary code until it returns;
reclaiming that code while frames and host syntax are live remains open.

Forced-collection tests cover nested evaluation, retained caller locals,
closures, namespace capture, syntax errors, handlers, macro reentry, failed
compilation side effects and opaque macro results. ASan/UBSan pass with leak
checking disabled for the tracing environment. Public modules now pass 16
checks and syntax quotation passes 18. The public inventory is 7 passing and
42 failing groups. The optional-parameter suite now exercises a working eval,
removing the earlier missing-primitive caveat. Full core compliance remains open.

### Numeric operations and float output

The runtime now supplies quotient, abs, min/max, rounding, transcendental
functions, exponentiation, exact/inexact conversion and predicates, floating
classification, and the five specified bitwise operations. Integer-valued
floats satisfy integer? while int? still tests their representation. Unary
division returns a float, including (/ 1).

Integer addition, subtraction, multiplication, negation and non-negative integer
powers now wrap at the 63-bit payload width on this host. This replaces the
prototype's earlier overflow errors and follows section 10.1 and Appendix A of
the shared language specification. Operations use unsigned arithmetic to avoid
signed overflow. Left shifts and float-to-integer conversion still reject
out-of-range results. Rounding uses half-away-from-zero for round.

Display, write and number->string now preserve the float representation with a
trailing .0 for integral fixed-point results. Formatting finds the shortest
significant-digit precision that round-trips through the host conversion, then
uses the primary runtime's fixed/exponent thresholds and exponent spelling.
Ten thousand generated finite bit patterns round-trip in the numeric test.

The public arithmetic suite passes 198 checks and bounded bitwise passes seven.
The numeric differential suite agrees with Chez on 15 programs and nine errors.
Bounded wrapping and the spec's integer-only quotient/exact conversion have
separate tests: Chez has bignums and rationals, and some of its transcendental
functions preserve exact results where the shared spec calls for floats.
Numeric boundary and float-format tests pass ASan/UBSan with leak checking
disabled for this environment. Runtime, nested-eval and compiler-free FFI
regressions pass. The public inventory is now nine passing and 40 failing
groups; sequence tests reach the still-missing string comparison operations.

### Strings, characters and buffers

Substring now clamps both bounds, accepts an omitted end, and indexes UTF-8 by
code point. String collection lookup returns characters. Added string ordering,
display concatenation, numeric parsing, string-to-list conversion, symbol/name
coercion, character/code-point conversion and ASCII character classification.
Display emits the character itself; write and nested values use reader syntax.
Strings escape nul and escape characters with the documented spellings.

The managed buffer has a separate byte-storage object that grows geometrically.
It tracks byte length and character length. Append formats arguments before any
managed allocation, roots the buffer while growing storage, and reloads managed
pointers after allocation. buffer->string copies a snapshot and leaves the
buffer usable. Forced-collection tests retain a snapshot across 500 appends.

Split, join, trim, replace, case conversion, list-to-string and string construction
are implemented in prototype/primitives.ot. They reuse the shared core's search
and sequence operations. Empty replacement patterns leave the source unchanged;
empty split separators are errors. The helpers currently inherit the shared
search implementation's allocation costs; bulk string performance remains to
be measured. Collection membership now handles strings and lists, keyword calls
return their default for non-table targets, and copy preserves nil.

The public sequence, printing, JSON, loop, reader-datum and OTcl suites now pass.
The inventory advances to 15 passing and 34 failing groups. Forced-collection
and ASan/UBSan string/buffer checks pass (leak checking disabled in this tracing
environment), along with runtime, shared-expander, eval and compiler-free FFI
regressions. Language and quasiquote differential suites still pass. Character
and reader coverage still needs type reflection and EOF support; full classes,
parameters, conditions, ports and remaining core operations are unfinished.

### Type, EOF and binding reflection

Added type, eof-object/eof-object?, buffer?, current-ns and describe. EOF is a
truthy immediate singleton and prints as #<eof>. Type distinguishes the
implemented public representations, including integral floats and macros.
The C specification calls the macro type :macro; Chez currently reports
:native for the same value, so that assertion has a separate conformance test.

Binding documentation is stored with the global, copied from value, function
and macro declarations. A successful ordinary redefinition replaces or clears
it. A skipped defonce keeps its original documentation, and an initializer
failure leaves the old binding and documentation intact. Embedded nul bytes
in docstrings are preserved. Namespace reflection uses the active namespace,
including when called from a function defined in another namespace. Qualified
and aliased names resolve to the owning binding for describe. A direct namespace
name in describe resolves here; the Chez probe returned nil for that spelling
without a require alias. The public alias-based module checks agree.

Reader-value testing also exposed missing variadic mutation: put! now accepts
multiple key/value pairs for tables and arrays. Argument count is checked
before mutation; each pair then takes effect in order.

Public reflection, bootstrap, library-module and character suites now pass;
the character suite contains 65 checks. The public inventory is 19 passing and
30 failing groups. Reader-values advances to handler-bind, which remains
unimplemented. Forced-collection and ASan/UBSan tests cover EOF, type names,
namespace lookup, repeated documentation replacement, failed initialization,
and variadic table/array mutation. Leak checking remains disabled for the
tracing environment. Runtime, module, reload and compiler-free FFI regressions
pass. Full core compliance remains open.

### Dynamic parameters

Added top-level defparam and with-params. Each declaration creates a fresh
managed parameter containing its name and default. Bindings are installed
sequentially, including evaluation of the next parameter expression. Calling
a parameter reads its innermost binding or its default. Declaration docs are
available through describe, and type reports :param.

The runtime keeps parameter/value pairs in a precisely rooted array. Normal
scope exit truncates it; VM try frames record the depth to restore on unwind.
Nested VM calls and eval preserve the caller's depth and restore it on failure.
Scope bodies retain a VM continuation until their bindings are removed, so
unwinding does not recurse through the C stack. Closures returned from a scope
read the dynamic value when later called; they do not capture that scope's
parameter value.

The C specification requires zero-argument parameter calls, so calls with
arguments raise E0301. Chez also permits setter calls and reports parameters
as :function. These representation/setter differences have separate C tests;
the shared binding semantics agree on 14 differential programs and nine errors.

The public expander suite now passes all 74 checks. Parameter tests exercise
nested and repeated bindings, initializer failure, nested try/eval, expansion
reentry, fresh declaration identity and restoration after an unhandled host
call. Forced-GC and ASan/UBSan pass with leak checking disabled in this tracing
environment. Four thousand nested scopes unwind on a 256 KiB C stack. Runtime,
condition, eval and compiler-free FFI regressions pass. Restart, quit and cleanup
transfers are still unimplemented and will need to restore dynamic state too;
this milestone does not claim those exit paths are complete.

The refreshed public inventory is 20 passing and 29 failing groups. Existing
condition and eval differential suites also pass (17/3 and 20/5 programs/errors).

### Signal-site handlers

Added handler-bind and signal. Predicate/handler expressions all run before
installation; installed bindings are visited innermost-last. A separate
visible-handler chain masks the current binding and all inner bindings while
its predicate or handler runs. Newly installed handlers link to the visible
outer chain, while masked bindings remain rooted for the original walk.
Handler return values are ignored, and signal returns nil when all decline.

Explicit errors and VM errors are offered before any stack or dynamic-parameter
unwinding. A handler may raise a replacement condition. Once an error has been
offered, an unmatched try rethrow does not offer it again. VM callback boundaries
also process errors from primitive callables while the handler mask is active;
this avoids reentering a failing handler when its callable is itself a builtin.
Try frames and nested calls restore both the physical handler depth and visible
chain. Reentrant VM callbacks are bounded at 128 to protect the C stack.

Reader-values now passes 64 public checks. The first 16 checks of the public
condition program also pass, through handler masking, initialization ordering,
and signal-once behavior; the remaining restart/cleanup/exit checks are still
unfinished. The refreshed public inventory is 21 passing and 28 failing groups.
Fourteen handler programs and five error cases agree with Chez. Forced-GC and
ASan/UBSan checks pass with leak checking disabled for the tracing environment.
A walk through 4,000 handlers passes on a 256 KiB C stack, and deliberately
recursive signals hit the callback limit with a catchable error. Parameter,
condition, eval and compiler-free FFI regression tests pass.

### Bytevectors and UTF-8

Added fixed-length mutable bytevectors with inline bytes and identity equality.
Constructors, indexed access, mutation, copying, overlapping copy!, append,
printing, reflection and Bytevector method dispatch are implemented. Generic
length/count/get/put! work on bytevectors; copy returns a fresh value. Arguments
and slice bounds are checked before allocation or copying. Variadic put! checks
arity first and applies valid key/value pairs in order.

UTF-8 conversion uses character offsets for string slices and byte offsets for
bytevector slices. Decoding rejects stray continuation bytes, overlong sequences,
surrogates, values above U+10FFFF and truncated sequences. Errors carry the
existing core decode-error type and an absolute byte position. Allocation paths
reload source values from rooted VM arguments after collection.

All 63 public bytevector checks pass. Forced-collection and ASan/UBSan tests also
cover scalar boundaries, sliced decode errors, condition inheritance, mutation
ordering and retained identity keys. Leak checking is disabled in the tracing
environment. Ten programs and nine error cases agree with Chez. String, handler,
runtime and compiler-free FFI regressions pass. The public inventory is now
22 passing and 27 failing groups, with no previously passing groups lost.
Ports, resources, filesystem operations and the remaining language and condition
features are still unfinished. Optional LLVM tools were unavailable for gc-lint.

### Preloads and incremental REPL input

The CLI now accepts repeated --load files in order before the main script or
expression, --repl after a script, and --path as an alias for -L. No script starts
the REPL. Preload failure stops startup. File loading reads streams without
requiring seek support and enforces a 16 MiB source limit.

The reader distinguishes incomplete forms from invalid input. The REPL buffers
an incomplete form, evaluates each completed form once, retains definitions and
continues after syntax or evaluation errors. EOF in an incomplete form is an
error. Prompts are emitted only for terminal input. The CLI tests cover repeated
preloads, failure ordering, multiline strings and dotted lists, multiple forms
per line, side effects across continuation lines, error recovery and empty files.
These tests and the reader fixtures pass under ASan/UBSan with leak checking
disabled in the tracing environment.

The public reader REPL group now passes. Condition tests can get past option
parsing; common.ot currently stops at missing restart-case. The cleanup-error
fixture previously accepted any caught error, so a missing unwind-protect could
produce a false pass. It now requires the actual cleanup symbol; Chez passes
that stronger assertion and the prototype correctly fails it. The refreshed
inventory is 23 passing and 26 failing groups. Cleanup, restart and exit semantics
remain unfinished.

### Restart scopes and transfers

Added restart-case, invoke-restart, find-restart, compute-restarts, restart-name
and restart-description. Managed restart objects retain their symbol name,
optional description and clause closure. Active scopes have a rooted object
array and VM snapshots. Lookup is innermost-last, including duplicate names in
one scope; an exact object selects that restart and expires when its scope exits.

Invocation carries a rooted target and argument list through nested VM callback
boundaries. The VM restores the target scope's frames, handlers, parameters and
try depth, then enters the clause while keeping that scope's restarts active.
Sibling invocation and repeated invocation reuse the recovery point rather than
accumulating C calls. Nested eval preserves the caller's restart scopes. Try
unwinding and host evaluation boundaries discard exited scopes.

The first 37 public condition checks pass, through restart introspection and
clause reinvocation. This is partial evidence: unwind-protect and exit are still
missing, and restart transfers do not yet run intervening cleanups. Seventeen
programs and eight error cases agree with Chez. Forced-collection and ASan/UBSan
checks cover dynamic lookup, expired objects, error-handler recovery, parameter
restoration, nested eval, captured locals, arity errors and 10,000 reinvocations.
Leak checking is disabled in the tracing environment. A transfer across 4,000
nested restart scopes passes on a 256 KiB C stack. Handler, eval, runtime and
compiler-free libffi regressions pass. The inventory remains 23 passing and 26
failing groups. Optional LLVM tools were unavailable for gc-lint.

### Cleanup and exit transfers

Added unwind-protect and defer, plus call-with-cleanup for the shared resource
forms. Cleanup closures are rooted separately from their VM scope snapshots.
Normal scope exit runs cleanup once while retaining the result. Error and
restart unwinding run intervening cleanup in reverse order before reaching a
catch or restart clause. The snapshots restore the dynamic parameters, handlers,
restarts and try depth visible at the cleanup site. Nested eval and handler
callbacks run their cleanup before passing a transfer back to the suspended VM.

Unwind-protect cleanup errors replace the pending result or transfer. Resource
cleanup receives nil for normal return/restart, the condition for error, or
:quit for exit. It preserves a pending transfer if cleanup raises, as specified
in specs/io.md. Chez currently replaces that transfer. The differential test
asserts both observed outcomes explicitly for the error and restart cases;
this discrepancy is not counted as shared behavior.

Exit and quit validate status 0 through 255 and use an uncatchable VM transfer.
Handlers and catch clauses are skipped, while cleanup runs. The embedding API
returns false and exposes the request through runtime_exit_status; it never
terminates the host process. The CLI returns the requested status, including
from preloads and REPL input. A cleanup error can cancel and replace an exit.
Interrupt delivery is still unimplemented.

All 64 common condition checks pass, along with public exit, quit and
cleanup-error tests. The inventory is 27 passing and 22 failing groups.
Forced-collection and ASan/UBSan checks cover ordering, replacement errors,
resource outcomes, nested callbacks and host exit status. CLI exit tests also
pass under ASan/UBSan; leak checking is disabled in the tracing environment.
Four thousand nested protected calls unwind on a 256 KiB C stack. Fourteen
programs and eight error cases agree with Chez, with the two resource-cleanup
differences checked separately. Restart, handler, eval, runtime and compiler-free
libffi regressions pass. Optional LLVM tools were unavailable for gc-lint.

### Cooperative interruption

The embedding API accepts a host-owned volatile sig_atomic_t flag. A signal
handler only sets the flag; runtime safe points consume it without allocation.
The CLI installs a SIGINT handler and restores the previous handler at exit.
The VM checks between instructions, and allocation and host evaluation/call
entry also poll. The no-request path avoids an out-of-line poll call in the
VM loop. Native calls are not forcibly terminated; blocking-port integration
remains unfinished with the port implementation.

An interrupt uses a distinct uncatchable transfer rather than a condition or
exit request. Cleanup runs, resource cleanup receives :quit, and the CLI returns
130 unless cleanup replaces the transfer. Embedders can inspect
runtime_interrupted, then reuse the runtime; the next top-level evaluation
clears the previous result without dropping a newly requested interrupt.
No interactive break loop is provided.

The public interrupt test passes. Process tests deliver SIGINT only after a
readiness marker and check nested cleanup order, resource outcomes, replacement
errors, recovery through a restart and nested eval. Host tests cover flag
consumption, detachment, subsequent evaluation and pending interrupts before
builtin calls or empty input. Interrupt and CLI sanitizer tests pass with leak
checking disabled in the tracing environment. Cleanup, restart and compiler-free
runtime/libffi regressions pass. Blocking port interruption is still unverified
because ports are not yet implemented.

### Monotonic and civil clocks

Added current-jiffy, jiffies-per-second and current-second. Jiffies use the
monotonic clock in nanoseconds and reject values beyond the integer range;
civil time uses the realtime clock and returns a float. All three reject
arguments. The shared otium.time/seconds helper now runs unchanged.

All 136 checks in tests/library/common.ot pass. The public inventory is now
29 passing and 20 failing groups. Forced-collection and ASan/UBSan tests bracket
runtime readings with host clock readings and check representation, frequency,
argument validation and reuse after errors. Leak checking is disabled in the
tracing environment. Three clock-contract programs and three error cases agree
with Chez. Ports, user classes, records, reader token support and their dependent
features remain unfinished.

### Memory ports and current streams

Added managed Port values, input/output predicates, close operations, memory
string/bytevector constructors and output extraction. Input ports snapshot their
source; output extraction returns a fresh value. Output storage grows in managed
raw-byte objects. Closed operations raise io-error with :reason :closed, and
ports dispatch through the existing Port class and close method.

Byte and character reads share one position. UTF-8 peeking leaves the bytes
unconsumed, and invalid decoding reports an absolute byte position without
consuming the invalid sequence. Implemented read-line, read-all, counted string
and bytevector reads, read-bytevector!, and character-set scanning. Successful
read-bytevector! does not allocate managed storage. Output supports characters,
bytes, string/bytevector slices, display-to, write-to and flushing.

Current input/output/error are dynamic params initialized with borrowed standard
streams. Printing now uses current-output-port, so the shared
with-output-to-string implementation works, including nested capture and error
unwinding. Standard output flushes after each operation. Standard input retains
up to four lookahead bytes; interrupted blocking reads propagate the runtime's
interrupt transfer and run cleanup. Standard stream handles remain host-owned;
closing a wrapper marks it closed without closing the host handle.

Sixty-eight applicable public port checks pass under forced collection and
ASan/UBSan, alongside snapshot, growth, dynamic-capture, decode-position and
allocation-free read-bytevector! checks. Leak checking is disabled in the tracing
environment. Ten programs and seven error cases agree with Chez. Stateful reads
in those comparisons are sequenced explicitly: Chez and the prototype evaluated
some println operands in different orders. Standard-stream subprocess checks
cover mixed byte/character reads, raw byte output and blocked-read interruption.
Interrupt and compiler-free runtime/libffi regressions pass.

The full port suite still stops at missing read. File ports, custom callbacks,
open-port tracking and datum reading remain unimplemented. The public inventory
remains 29 passing and 20 failing groups, with no previously passing group lost.
Optional LLVM tools were unavailable for gc-lint.

### Datum reading from ports

Added read with an optional current-input-port default. A streaming scanner
collects one datum boundary, tracking strings, escapes, comments, quote prefixes,
character literals and collection delimiters. The existing reader still owns
syntax validation and value construction. The scanner does not reparse growing
prefixes and leaves the following delimiter or datum unread. Completed strings,
characters and collections return without waiting for EOF. Mismatched closing
delimiters are passed to the reader immediately.

All 174 public port-datum checks pass. Forced-collection and ASan/UBSan tests
also cover 52 applicable negative reader fixtures, adjacent datums, quoted
comments, malformed UTF-8 positions, empty input and a 5,000-element list.
Leak checking is disabled in the tracing environment. Stdin subprocess tests
keep the input pipe open and verify that completed datums return and leave the
next byte available. The expanded port differential suite agrees with Chez on
14 programs and 11 error cases. Reader, memory-port, runtime and compiler-free
libffi regressions pass.

The public inventory is 30 passing and 19 failing groups. The broad port suite
now reaches make-input-port. Custom callbacks, file ports and open-port tracking
remain unfinished, along with classes, records and reader token support.

### Custom ports and open-port tracking

Added make-input-port and make-output-port with byte-level callbacks, optional
flush/close callbacks and a 4 KiB managed bytevector buffer. Input retains unread
bytes across partial fills, including UTF-8 lookahead. Output tracks the confirmed
write offset, so retrying a failed flush does not replay bytes already accepted
by the sink. Full buffers, explicit flush and close drain pending output.
Callback return counts are checked, and same-port callback reentry is rejected.

Port operations now keep both the active port and caller payload in rooted
slots. Helpers receive the port slot and reload object storage after callbacks;
callback functions, argument lists and buffers are rooted across nested VM calls.
A restart transfer from a callback clears its busy flag before unwinding reaches
surrounding cleanup. Callback tables are captured when the port is constructed.

The open-port registry retains custom and standard ports until close and returns
a detached array. Memory ports are excluded. Finalizers remain deferred. Closing
an already closed port does not call its callback again. Runtime shutdown flushing
of all registered output ports is still unfinished and will be added with the
file-port lifecycle work.

All 89 public port checks pass under forced collection and ASan/UBSan. Extra
checks cover callback reentry, invalid counts, callback failure/retry, output
larger than the buffer, detached registry snapshots, restart transfer and datum
reading through single-byte callback fills. Leak checking is disabled in the
tracing environment. Nineteen port programs and eleven error cases agree with
Chez. Datum-reader, interrupt and compiler-free runtime/libffi regressions pass.
The public inventory is 31 passing and 18 failing groups. Optional LLVM tools
were unavailable for gc-lint. File ports and remaining language features are
still unfinished.

### Shutdown flushing

Added runtime_flush_ports for idle host boundaries. It roots a snapshot of the
open-port registry, flushes each still-open output port, and preserves the prior
evaluation or exit result on success. A flush failure replaces that result and
stops the pass; retrying uses each port's confirmed write offset. Callback changes
to the registry do not invalidate the snapshot. Ports opened by a flush callback
are left for a subsequent pass. Runtime destruction does not invoke Otium code.

The CLI flushes on normal script/REPL completion and explicit exit/quit. Successful
flushing preserves the requested status. A failed flush reports an error, while
an exit or interrupt raised by a callback supplies the resulting status.

Forced-collection and ASan/UBSan tests cover partial-write retry, preservation of
exit status, and a callback closing another registered port. CLI tests verify
buffered output at normal completion, exit and quit, plus failing and exiting
flush callbacks. Leak checking is disabled in the tracing environment. The port
differential suite still agrees with Chez on 19 programs and 11 errors. The
public inventory remains 31 passing and 18 failing groups. The public file-based
exit-flush case still needs file ports and CLI script arguments, so this milestone
does not count it as complete.

### Script arguments

Added the builtin otium.process namespace with arguments and script-name. The
host setter copies script metadata into rooted runtime values and publishes it
only after all allocations succeed. Each arguments call returns a fresh list.
The CLI installs metadata before preloads and preserves every token after --,
including empty strings, Unicode and tokens that look like runtime options.
Using -- before the script also ends option parsing. With no script, script-name
returns nil; a supplied script name retains the caller's spelling.

Forced-collection tests cover host buffer ownership, repeated reads, failed
replacement, empty metadata, module aliases/refers/reload and recovery after an
arity error. CLI tests cover the public argument fixture, expression arguments,
separator placement and metadata visible to preloads. Both suites pass under
ASan/UBSan with leak checking disabled in the tracing environment.

The public inventory remains 31 passing and 18 failing groups. File-port tests
now reach missing file-exists? and open-output-file instead of stopping at CLI
argument parsing. The format corpus reaches the missing otium.reader module.
File ports, filesystem operations, classes, records and reader tooling remain.

### File ports

Added input/output file ports and binary aliases, optional buffer sizes, truncate,
append and exclusive-create modes. Buffer sizes below four round up to four.
File ports enter the open-port registry, flush on close and runtime shutdown,
and release their descriptors and host buffers on close or runtime destruction.
Standard streams remain borrowed. NUL-containing paths are rejected before open.
Directory opens fail with :is-directory. Host failures carry file-error conditions
with operation, path, reason and host detail.

The first file backend uses libc streams and stable host-allocated buffers.
The specified embedding I/O callback interface and managed bytevector buffering
remain unfinished; this backend does not yet satisfy that embedding contract.

Forced-collection and ASan/UBSan tests cover file modes, shared read-file/write-file,
small-buffer UTF-8 lookahead, registry removal, repeated close, failed opens,
shutdown flushing and /dev/full reporting :no-space when available. Leak checking
is disabled in the tracing environment. Existing memory/custom/standard port and
shutdown tests pass. Optional LLVM tools were unavailable for gc-lint.

The public inventory is 34 passing and 15 failing groups. Read-file, write-file
and file-based exit flushing now pass. Filesystem helpers still block the broad
file suite; classes, records, reader tooling and embedding I/O remain unfinished.

### Filesystem operations

Added file-exists?, delete-file, rename-file, file-info, list-directory and
make-directory, plus file-symbolic-link? and current-directory for the shared
filesystem helpers. Metadata follows symlinks; the link predicate uses lstat,
so dangling symlinks remain distinguishable from missing entries. Listings are
sorted arrays without dot entries. Paths containing NUL are rejected. Host
failures use the existing structured file-error conditions. Directory scanning
polls for interrupts and releases its host allocations on failure.

All 32 public file-port checks now pass under forced collection and ASan/UBSan.
Additional checks cover symlinks, dangling links, deleting a link without deleting
its target, metadata errors, sorted listings, current-directory and NUL rejection.
Leak checking is disabled in the tracing environment. Runtime and libffi tests
also pass with PATH and OTIUM_NATIVE_CC set to nonexistent locations. Optional
LLVM tools were unavailable for gc-lint.

The public inventory is 36 passing and 13 failing groups. Both broad file-port
and filesystem-helper groups now pass. Remaining public failures depend on user
classes/records or reader tooling. The embedding I/O contract remains unfinished.

### User classes and instance migration

Added defabstract, defclass and reset-class! with stable descriptors, abstract
parent validation, cycle rejection and separate slot layouts. Instances hold
class, layout and slot-storage references. Construction validates keyword/value
arguments, evaluates defaults per instance and supports required slots and explicit
accessors. Instances are callable by slot keyword, support assignment and shallow
copy, dispatch through their class ancestry, and compare by identity. Shared
with-slots and is-a? now work with user instances.

Slot reads, writes and copies migrate stale instances. Kept slots retain values;
added slots and changed state flags use initializers. update-instance receives
old slots and can supply newly required values. Failed initializers or hooks
restore the old layout and slot storage, so the next access retries. Instance
identity and table keys survive migration. Changing defaults without changing
slot names/order/state updates future construction without migrating instances.

Forced-collection and ASan/UBSan checks cover the initial construction/access
section of the public class suite plus migration, rollback/retry, required slots,
identity, resets, parent changes and state-flag changes. Leak checking is disabled
in the tracing environment. Eight class programs and eight error cases agree
with Chez. Existing primary generic tests pass. Optional LLVM tools were
unavailable for gc-lint.

The public inventory is 40 passing and 9 failing groups. Matching, resources,
resource exit and blocking-input interruption now pass. The class suite reaches
lexical next methods, which remain unfinished along with around methods, property
lifecycle operations, records and reader tooling. Slot inline caches, generic
dispatch caches and the embedding I/O contract also remain unfinished.

### Lexical next methods and around methods

Added :next bindings and :around qualifiers. A next-method procedure retains a
detached list of method objects applicable to the original call. Replacement
arguments select from that list; later additions, removals, replacements and
generic resets do not change an escaped procedure's methods. Around methods
run outside primaries. Ambiguous remaining methods fail when next is called.
Methods without :next avoid allocating a snapshot.

The VM installs each selected method directly, passing its next binding as a
hidden lexical argument. Tail calls through next methods stay in the VM rather
than growing the C callback stack. Added qualifier-aware replacement/removal,
detached introspection and reset-generic!. Next binding names must be local,
distinct from parameters, and followed by a body. The public suite also exposed
constructor duplicate-key semantics; construction now uses the last value.

All 151 public class checks pass normally, under forced collection, and under
ASan/UBSan. Extra tests cover escaped snapshots after removal/replacement/reset,
replacement arguments that exclude originally inapplicable methods, arity errors,
procedure reflection and 2,000 tail calls. Leak checking is disabled in the
tracing environment. Eight method programs and seven errors agree with Chez;
the existing ten generic programs/four errors and instance migration tests pass.
Optional LLVM tools were unavailable for gc-lint.

The public inventory is 41 passing and 8 failing groups. Five currently stop at
missing record support, and three at the missing reader module. Property/state
lifecycle operations, records, reader tooling, slot/dispatch caches and embedding
I/O remain unfinished.

### Fixed-layout records

Added the runtime primitives used by the shared defrecord/declare-record!
implementation. Descriptors copy field names into keyword metadata; records hold
the descriptor and positional values inline in managed storage. Reads work through
record-ref, direct calls and apply. Writes return the value and reject immutable
records. Applied assignment uses the shared dispatch helper. Record identity and
hashing remain stable across mutation and collection. Runtime reflection reports
record and record-type separately.

The shared declaration registry supplies namespace identity and reload checks;
no second declaration registry was added in C. It preserves compatible descriptors
and rejects field or mutability changes before replacing a binding. The low-level
descriptor constructor checks proper acyclic field lists, symbol names, duplicate
fields and boolean immutability.

All 45 public record checks pass normally, under forced collection and under
ASan/UBSan. Extra checks cover retained record chains, field-list mutation after
descriptor construction, cyclic/improper lists, shallow immutability and stable
table keys. Leak checking is disabled in the tracing environment. Six record
programs and eight error cases agree with Chez. All 151 class checks and method
snapshot/tail-call regressions still pass. Optional LLVM tools were unavailable
for gc-lint.

The public inventory is 43 passing and 6 failing groups. Records and the UI
calculator now pass. Three groups stop at class-properties and three at the
missing reader module. Property lifecycle, reader tooling, class caches and
embedding I/O remain unfinished.

### Property descriptions and mounted state

Added class-properties, class-mount and class-update-properties!. Property
construction evaluates property defaults, rejects state arguments and leaves
state slots nil. Mounting copies properties and evaluates state initializers in
declaration order, so separate mounts get separate state. Updating an unchanged
layout copies only properties and preserves state.

Layout migration accepts supplied properties before running initializers and
update-instance. New required properties can therefore come from the caller;
kept state survives, and state-flag changes reset according to the new role.
Initializer or hook failures restore the previous layout and all slot values.
Mount/update reject a source description whose layout is outdated. Direct slot
access still performs normal instance migration, matching Chez.

All 136 public UI checks pass normally, under forced collection and under
ASan/UBSan. Additional checks cover initializer counts, repeated mounts, fresh
state, shared property values, hook visibility and migration rollback/retry.
Leak checking is disabled in the tracing environment. Six lifecycle programs
and five errors agree with Chez. Class/method and ordinary migration regressions
pass. Optional LLVM tools were unavailable for gc-lint.

The public inventory is 46 passing and 3 failing groups. Every inventoried UI
group now passes. The remaining groups need otium.reader token tooling and the
formatting library that uses it. Class caches and embedding I/O also remain
unfinished; passing this inventory alone will not establish full completion.

### Reader tokens and formatter integration

Added the builtin otium.reader/tokens module. It validates source using the
existing datum reader, then emits raw token text with kind, zero-based line and
Unicode-scalar column, and newline gaps. Comments, prefixes, collection openers,
character literals and multiline strings retain their source spelling. CRLF is
one line break, including when a character token ends at its CR. The validator
releases its AST arena between top-level forms. Token construction roots all
partially built lists and preserves earlier results across later calls.

All 120 public token checks pass normally, under forced collection and under
ASan/UBSan. Extra checks cover a 2,000-token source and retained output lists.
Every printed token field in the public fixtures agrees with Chez. Leak checking
is disabled in the tracing environment. Optional LLVM tools were unavailable
for gc-lint.

The default public inventory is 48 passing and 1 failing group. Reader tokens
and formatting checks now pass. The remaining corpus test exhausts the CLI's
fixed 1 MiB semispace. Running the unchanged corpus with --heap=16777216 passes:
116 files, zero failures, 382 collections and at most 118 VM frames. The default
CLI memory policy still needs work; this configured run does not erase the
remaining default-run failure. Class caches and embedding I/O are also unfinished.

### Bounded CLI heap growth

The CLI now starts with 1 MiB semispaces and permits growth to 64 MiB per space.
An explicit --heap remains fixed unless --heap-max is also supplied. Embedding
heaps remain fixed until the host calls runtime_heap_limit. Allocation collects
before growth, doubles within the limit, and allocates both replacement spaces
before relocating roots. Cycles, aliases and object identity survive relocation.
Failed allocations leave the existing heap usable. Stats include current
semispace capacity. The memory contract above describes transient allocation.

New tests cover growth through multiple capacities, cyclic roots, opaque payloads,
identity, cap rejection, exhaustion/recovery and collecting garbage without growth.
CLI checks cover default growth, explicit fixed heaps, maximum limits and invalid
option combinations. ASan/UBSan growth and CLI checks pass, with leak checking
disabled in the tracing environment. The full prototype test target passes.
Its namespace leak check now measures the four namespaces it creates relative
to startup, accommodating the new builtin process and reader namespaces.
Optional LLVM tools were unavailable for gc-lint.

All 49 inventoried public groups pass with default CLI settings. The unchanged
format corpus reports 116 files and zero failures, growing to a 16 MiB semispace
with 768 collections and at most 118 VM frames. Runtime/libffi tests also pass
with PATH and OTIUM_NATIVE_CC pointing to nonexistent locations.

This establishes the current inventory, not the full completion audit. Broader
FFI public tests, embedding I/O and class performance requirements still need
review and implementation where missing.

### Shared FFI module and bulk memory

The canonical builtin namespace is now otium.ffi. The legacy ffi spelling still
resolves to it, while requiring otium.ffi loads the shared library. Added the
scalar low-level hooks used by that library for binding, allocation, typed byte
offsets and loader diagnostics. Native FFI failures now become ffi-error
conditions. Detailed call-site error metadata is still incomplete.

Added copy!, read-bytes, write-bytes! and read-utf8-bytes. Copying supports
overlapping regions; reads return fresh managed values. Exact UTF-8 reads preserve
embedded NULs and reject malformed or incomplete encodings. All operations check
pointer lifetime and view bounds, including zero-length operations.

All 23 public bulk checks pass normally and with forced collection under
ASan/UBSan. Additional regressions cover both overlap directions, detached reads,
source/destination bounds and copying released storage. Existing foreign-call,
namespace and module tests pass. Leak checking was disabled in the tracing
environment; optional LLVM tools were unavailable for gc-lint. The core inventory
remains 49 passing groups. The public common FFI suite now reaches the missing
%layout primitive. Aggregate calls, callbacks and the rest of the broader FFI
audit remain unfinished.

### Aggregate memory layouts

Added opaque foreign layout values and the %layout/%layout-data hooks used by
the shared library. Layout calculations and field-path traversal remain in
Otium. The runtime copies description trees on construction and extraction, so
mutating declaration lists or a returned description cannot change a saved
layout. These traced snapshots survive collection independently of bindings.
Tree copying has bounded depth and work to reject cyclic or excessive input.

The first 93 checks of the public common FFI suite pass, through aggregate
memory access. This was measured by running its unchanged prefix before the
first by-value binding and adding a report call. The complete suite still stops
at that binding because aggregate ABI lowering is not implemented yet.
Additional forced-collection and ASan/UBSan tests cover mutated declaration
lists, nested arrays, saved layouts after rebinding, union offsets, field views,
duplicate fields and released storage. The foreign regression target passes;
leak checking remains disabled in the tracing environment. No optional LLVM
tools are available for gc-lint.

### By-value struct calls through libffi

Prepared signatures now own recursive libffi type trees for structs and nested
fixed arrays. Preparation checks size, alignment and field offsets against the
shared layout description using ffi_get_struct_offsets. Cache matching compares
the complete ABI tree as well as scalar conversion codes; equal-sized integer
and floating layouts cannot share the wrong call interface. Host trees are
released with their cached signatures. See the upstream [libffi size and
alignment contract](https://www.chiark.greenend.org.uk/doc/libffi-dev/html/Size-and-Alignment.html).

Aggregate arguments are validated and copied into separate aligned host storage.
Aggregate result buffers and owning wrappers are allocated before native
execution. Returned bytes go into a fresh explicitly releasable allocation;
subsequent calls and source mutation cannot overwrite it. Null release is a
no-op. External pointers permit caller-asserted spans, while derived views carry
bounds and address arithmetic rejects overflow.

The unchanged public common FFI prefix through struct calls passes 156 checks.
The complete suite currently stops at missing library metadata in a conversion
condition. Dedicated tests cover floating and mixed-register structs, a large
nested-array result, short/misaligned/released arguments, independent results,
layout rebinding, structural signature reuse and bounded cache reclamation.
The core inventory remains 49 passing groups, and foreign reload tests pass.
Union ABI classification and callbacks remain unimplemented. Union-containing
signatures currently fail with abi rather than using an incorrect substitute.

The expanded foreign tests pass under forced collection and ASan/UBSan,
including the aggregate signature-cache checks. Leak checking is disabled in
the tracing environment. Optional LLVM tools remain unavailable for gc-lint.

### FFI call error context

Library wrappers retain the selected path, including when multiple paths share
a loader handle. Bound functions trace their library wrapper, symbol and copied
type descriptions. Call errors now identify the selected library, native symbol,
failing argument index and expected scalar or aggregate type. Result conversion
errors have no argument index; arity errors have neither an index nor an expected
type. Native binding errors also carry available library and symbol information.
Saved conditions and aggregate type descriptions survive collection and rebinding.

Binding publication now reads the current host binding-list head after all
managed allocations. Reading it before allocation could retain a stale link if
collection reclaimed an older binding while constructing the new wrapper.

All 166 public common FFI checks before union calls pass. The expanded foreign
regression includes 39 aggregate/error checks and 23 bulk checks, with additional
signature-cache checks. It passes under forced collection and ASan/UBSan; foreign
reload tests also pass. Leak checking is disabled in the tracing environment.
Optional LLVM tools remain unavailable for gc-lint. The full public FFI suite
still stops at union ABI classification; callbacks and ownership remain to be
completed. Memory-operation error metadata needs a further audit.


## Public resource, SQLite and CLI inventory

The compliance runner now imports resource, SQLite, diagnostics and CLI test
definitions as well as the original core and FFI groups. Its current result is
63 passes, five failures and no skips. Diagnostic codes/rendering and launcher
options, arguments and project paths remain failing gates. Runtime-independent
checks that inspect the original C sources are outside this clean-room run.

Resource cleanup errors replace the pending result, error, restart, quit or
interrupt. Earlier prototype tests and the I/O spec had preserved the original
transfer instead; both now agree with Chez and the public tests. Raised symbol
and keyword values retain their names in the native error summary. Both SQLite
suites pass, including 67 high-level transaction/resource checks.

The process namespace now provides validated getenv/setenv! and write-error.
Error output uses the dynamic current-error-port and the existing managed port
path. Public CLI library and greeting examples pass, with forced-collection
checks for copied environment values and redirected error output.

Cleanup, conditions, process, ports, interrupt, foreign callbacks and foreign
reload regressions pass. Cleanup, process, ports and interrupt sanitizer targets
pass with ASan/UBSan and leak checking disabled. The expanded compliance runner
continues to return failure until its diagnostic and launcher gaps are fixed.
