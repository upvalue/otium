# Prototype VM completion brief

Complete the clean-room runtime rewrite so it can run Otium's core language and
applicable public tests, uses libffi for FFI, and has no generated-C execution
tier or runtime compiler dependency. This is the existing objective; passing a
selected subset of tests is not sufficient evidence of completion.

For formatting and factoring work, see [codebase-cleanup.md](codebase-cleanup.md).
The goal was paused when these briefs were requested. Resume implementation when
this brief is taken up, rather than treating its creation as a request to resume.

## Starting state and constraints

Work is on `wip-runtime-rewrite`. Initial checkpoint: `26bd86b`,
`WIP: checkpoint clean-room C runtime rewrite`. Substantial subsequent work is
uncommitted. Inspect the worktree and preserve unrelated changes; the cleanup
brief lists the known unrelated paths. Do not blanket-stage the tree.

Never inspect the original `c/src` implementation or repository history. Use
public specs, docs, library code, examples, tests, fixtures and Chez comparisons.
Never edit README.md or AGENTS.md. Read AGENT-LANG.md before writing Otium.
Prefer shared Otium implementations over additional native escape hatches.

Read `docs/runtime-rewrite.md`, `.tickets/oti-6ybd.md`, `specs/ffi.md`, `specs/io.md`,
`lib/otium/ffi.ot`, and the relevant public tests. Use `ticket --help` for ticket
operations. Older design-log entries describe superseded implementations;
revalidate current behavior from source and fresh test results.

## Implemented and last verified

- Bytecode VM with closures, proper tail calls, namespaces and reload, shared
  macro expansion, conditions, handlers, restarts, cleanup and parameters.
- Collections, strings, bytevectors, numeric operations, ports, filesystem,
  process arguments, classes, generic/around/next methods, instance migration,
  property/state lifecycle, records, reader tokens and formatter integration.
- Moving semispace GC with explicit roots and optional bounded heap growth.
  CLI defaults to 1 MiB per semispace, growing to 64 MiB per space. Explicit
  `--heap` remains fixed unless paired with `--heap-max`; embedding heaps opt in.
- The generated-C backend is removed. `compiler.c` remains the bytecode compiler;
  `modules.c` remains the loader. Building the runtime and C fixtures requires a
  C compiler; executing the built runtime must not require one.
- FFI scalar calls, UTF-8, native memory and views, bulk copies, immutable layouts,
  by-value structs and nested arrays use libffi. The shared `otium.ffi` library
  loads over native primitives; legacy `ffi` names resolve to the same namespace.
- Signatures cache complete ABI trees. Aggregate arguments are copied, and owned
  result storage is prepared before native execution. Bindings retain selected
  library paths, symbols and copied type descriptions for error reporting.

Last evidence: 49/49 groups in `prototype/compliance.py`; 166 checks in the
unchanged prefix of `tests/ffi/common.ot` before union calls. The full common
suite fails at union ABI classification. The foreign regression passes with
forced collection and ASan/UBSan, including 23 bulk checks, 39 aggregate/error
checks and additional cache checks. Foreign reload tests pass. The full prototype
test target passed before the latest FFI expansion; rerun it for a fresh baseline.

The 49-group inventory excludes the broader FFI suites. Leak detection was
disabled in the tracing environment. Optional LLVM tools for gc-lint were absent.
These limitations must remain visible in completion claims.

## Remaining work, in order

### 1. Complete union calls

`prototype/foreign.c` currently rejects unions with an `abi` condition. The last
interrupted work only researched classification; no union implementation was
written. The observed host is x86_64 Linux.

Handle unions and union-containing structs/arrays by classifying all members.
Selecting the largest member or substituting an equal-sized byte array is not
ABI-correct in general. Preserve field placement and alignment when a union is
nested at a nonzero offset. Cover mixed integer/floating register classes,
small returns, stack arguments, indirect results, register exhaustion and saved
bindings after layout rebinding. Reject invalid zero-length aggregate members
with the specified condition.

The public fixture and common suite cover Number, Floats, Doubles, BigUnion,
MixedFloat, OffsetUnion, NestedUnion, TinyUnion, UnionArray, register pressure
and high-bit unsigned values. Use those tests intact. Consult primary ABI and
libffi documentation; any target-specific lowering needs explicit platform
limits and tests. Do not claim portability from an x86_64-only result.

### 2. Implement callbacks and external ownership

Read the callback and ownership contracts before designing their representation.
Use libffi closures for callbacks. Cover synchronous reentry, argument/result
conversion, callback lifetime, collection safety, nested calls, conditions,
cleanup and exit propagation. Foreign-thread callbacks need the specified
rejection behavior. Errors must not unwind arbitrarily through native frames.
The current call path assumes leaf calls; that assumption must be revised.

Run `tests/ffi/callbacks.ot` and the callback exit/thread cases registered in
`tests/ffi/tests.py`. Implement external pointer ownership/release and run the
ownership suite. Audit the provisional CPointer class alias and close dispatch:
borrowed, owned and callback pointer lifetimes need correct distinctions.
Native allocations require explicit release; GC does not release storage that C
may retain. Preserve derived-view invalidation and exactly-once release.

### 3. Finish FFI coverage and errors

Run every applicable group in `tests/ffi/tests.py`, including math, common,
callbacks, callback exit/thread, ownership and bulk. Add these to a durable
prototype compliance inventory with fixture and argument setup. Do not silently
exclude a failing suite or count a truncated prefix as a full-suite pass.

Call/bind error context is implemented. Audit memory-operation operation names,
argument indexes and expected types, loader diagnostics, failure cleanup,
integer limits, unknown-extent external pointers and aggregate validation.
Keep collection/reload stress coverage for native metadata and signature caches.

### 4. Finish embedding I/O and integration

File ports currently use libc streams and stable host buffers. The specified
host I/O callback interface and managed buffering remain unfinished. Implement
that contract and test partial reads/writes, buffering, close/flush, errors,
shutdown, interrupts and callback reentry. Preserve the existing memory/custom
port behavior and cleanup semantics.

Inventory public CLI/project behavior and compare the prototype launcher with
the supported contract. Existing project launchers still use the old runtime;
do not silently switch the default until compatibility has been demonstrated.
Record the remaining launcher/integration decisions and make any selected switch
explicit and testable. Chez remains the primary implementation.

### 5. Audit completion and performance

Build a requirements-to-evidence list from public specifications and tests.
Identify untested requirements, missing APIs, failed groups and explicit platform
limits. Test interactions: callbacks plus GC, errors plus cleanup, interrupts
plus blocking I/O, and live reload plus retained functions/layouts/resources.
Use Chez differential checks where appropriate, with documented reasons for
intentional differences rather than blindly copying every behavior.

Measure startup, steady-state performance, allocation and memory retention under
representative workloads and long reload cycles. Slot and generic-dispatch
caches are unfinished. Determine from the documented requirements and measured
workloads whether they are required now; avoid an open-ended optimization project.

Before declaring completion, rerun all applicable suites, affected sanitizers and
differential tests. Verify runtime execution with no compiler available. Reconcile
current documentation, retain explicit limitations, update the ticket and commit
coherent milestones. Do not mark the overall goal complete while required FFI,
embedding or integration work remains unverified.

## Useful commands

From the repository root:

```sh
make -C prototype -j2 test
python3 prototype/compliance.py --output /tmp/otium-prototype-compliance.json
make -C prototype test-foreign test-foreign-reload
ASAN_OPTIONS=detect_leaks=0 make -C prototype sanitize-foreign
make -C prototype otium-proto ffi_fixture.so ffi_other.so
prototype/otium-proto --load tests/ffi/support.ot --load tests/ffi/runtime-c.ot tests/ffi/common.ot -- "$PWD/prototype/ffi_fixture.so" "$PWD/prototype/ffi_other.so"
prototype/otium-proto tests/ffi/bulk.ot
git diff --check
```

Compiler-free execution check after building:

```sh
(cd prototype && PATH=/nonexistent OTIUM_NATIVE_CC=/nonexistent ./test_foreign)
```

Inspect `prototype/Makefile` for individual sanitizer and differential targets;
its default `sanitize` target does not cover everything. Run `make gc-lint` for
allocation/root changes when optional LLVM tools are available. Use prototype
GC tests when working on its collector, not the original C GC suite.

Managed values must be rooted across allocation and reloaded afterwards. Never
root the same slot twice. Host metadata can be reclaimed during collection;
prepare and publish native resources in an order that survives every allocation
failure and callback/control-transfer path.
