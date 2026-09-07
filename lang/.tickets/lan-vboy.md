---
id: lan-vboy
status: closed
deps: []
links: []
created: 2026-08-20T02:06:35Z
type: task
priority: 2
assignee: Phil
tags: [vm, performance]
---
# Use computed goto for VM dispatch

The VM currently dispatches printable ASCII opcodes through a chain of instruction tests in vm_execute. Add a computed-goto dispatch path for compilers that support labels as values, while retaining a portable fallback. Keep the printable ASCII bytecode encoding and VM semantics unchanged. Current fresh-process medians are fib 3.582 s, loop 349.73 ms, and tables 37.22 ms.

## Design

Keep the opcode inventory in one place so computed-goto labels and the fallback cannot drift. Detect compiler support or gate the extension with OT_COMPUTED_GOTO. The normal build uses -Wpedantic -Werror, so isolate any labels-as-values diagnostic suppression to the dispatch implementation. Do not move the VM out of src/otium.c.

## Acceptance Criteria

make test passes with computed goto enabled. A forced portable-fallback build also passes the runtime and conformance tests. make bench is run before and after on the same machine and the result is recorded. Bytecode returned by ot_function_bytecode remains printable ASCII and unchanged in meaning.

## Notes

**2026-09-07T03:01:53Z**

Implemented as part of the broader VM rewrite. Dispatch is generated from OT_BYTECODE_OPS, GCC/Clang use computed goto with direct handler chaining, and OT_COMPUTED_GOTO=0 is the tested fallback. The earlier printable-execution-byte requirement was superseded by the femtolisp-style decision to use compact binary execution operands; ot_function_bytecode now explicitly exposes compact bytecode. Same-host before/after results are recorded in vm-performance-report.md.
