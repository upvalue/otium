# Clean-room runtime

This implementation is independent of `c/`. Its initial semantic source is
`AGENT-LANG.md`. Do not consult the original C implementation or repository
history when extending it. Use the public language docs, library, examples and
tests to resolve semantics. The existing launchers still use the existing runtime.

Build with `make -C prototype`. Run a source file with
`prototype/otium-proto file.ot`, or print an expression's result with
`prototype/otium-proto -e '(+ 20 22)'`. `--heap=8192` sets each semispace's size;
the two spaces together consume twice that amount. `--stack=1024` caps VM value
slots. `--stats` reports collection count and maximum simultaneous VM frames.
`--native` enables the native tier after 1,024 bytecode instructions in a
function; `--native=1` compiles before its first instruction. Native statistics
include compiled function count, native entries and compiler wall time.
Foreign adapters report their signature count and compiler time separately.

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

The heap has a fixed budget of two semispaces. Allocation returns zero when a
request cannot fit after collection. It does not silently grow the budget.
Opaque payloads are zeroed; traced fields start as nil. The compiler should
eventually expose object initialization to optimization so unnecessary writes
can be eliminated without weakening the safepoint contract.

## Implementation sequence

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

## Native tier

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

Libffi development headers were unavailable. Instead, binding compiles a typed
C adapter with the host compiler and caches it by argument/result types. The
target symbol is passed separately, so one adapter can serve different libraries
and functions with the same signature. Adapters use the platform C ABI for mixed
integer/floating-point arguments and register/stack argument placement. This
requires a C compiler even when VM execution is selected. `module.c` provides the
shared compiler/loader path for adapters and native Otium functions. Signature
types are selected from a fixed table; library paths and symbol names are never
inserted into generated C source.

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
collection. Unused adapters have a bounded cache; live bindings retain their
adapters. Reopening the same loader handle reuses one library record. Libraries
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

## Checks

Run `make -C prototype test` and `make -C prototype sanitize`. In sandboxes where
LeakSanitizer cannot inspect `/proc`, use `ASAN_OPTIONS=detect_leaks=0` for the
second command; this retains address and undefined-behavior checks but does not
validate leaks. The first implementation also passes a Clang warnings-as-errors
build. Reader checks include the shared core source, public datum fixtures,
memory/depth limits, invalid UTF-8, numeric overflow and 50,000 deterministic
malformed inputs. Optional LLVM development tools were unavailable for GC lint.

`make -C prototype test-runtime` checks closures, lexical mutation, live global
redefinition, numeric boundaries, tail calls and recovery after errors. It also
exercises collection with an 8 KiB semispace, collection at every allocation,
and live-heap exhaustion. `sanitize-runtime` runs those checks under ASan/UBSan.
`make -C prototype differential` compares 57 supported programs and thirty-two error
cases with the primary Chez implementation. These checks exposed and corrected
the return values of `set!` and pair mutation, and the restriction on definitions
in expression positions.

`make -C prototype test-native` runs the same runtime suite with eager native
compilation and with a heat threshold of 31 to exercise transitions during
execution. It includes large frames, fresh captured bindings across tail calls,
redefinition during argument evaluation, and collection at every allocation.
The differential suite compares both the VM and native tier with Chez.
`ASAN_OPTIONS=detect_leaks=0 make -C prototype sanitize-native` instruments both
the runtime and generated native modules with ASan/UBSan. Native compiler
subprocesses use `OTIUM_NATIVE_SANITIZE=1` only for that test target.

`make -C prototype test-foreign` uses the public C fixture plus a mixed-argument
fixture. It covers all scalar widths, signature reuse across libraries, strict
conversion errors, UTF-8 validation/copying, native retention, view bounds,
release detection, and repeated calls in an 8 KiB semispace. The same tests pass
with GCC and Clang and in VM/native modes. `sanitize-foreign` instruments the
runtime, adapters and fixture libraries with ASan/UBSan. Clang's exact C function
type check is disabled only at the foreign adapter boundary: FFI pointer types
erase C pointee types, so that check rejects ABI-compatible declarations. Other
undefined-behavior checks and address instrumentation remain enabled.
`make -C prototype differential-foreign` compares twelve programs and six error
cases across VM, native and Chez execution.

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
