# VM performance report

Point-in-time snapshot: 2026-09-07

This records the bytecode and call-path rewrite against the untouched parent
revision on the same x86_64 Linux host. Both builds used the default semispace
collector, `-O2`, one warmup, and five measured fresh-process runs.

| benchmark | before | after | speedup |
| --- | ---: | ---: | ---: |
| `destruc` | 1.003 s | 501.96 ms | 2.00x |
| `fib` | 5.690 s | 2.949 s | 1.93x |
| `gcbench` | 671.11 ms | 469.05 ms | 1.43x |
| `loop` | 527.35 ms | 186.86 ms | 2.82x |
| `mperm` | 1.511 s | 920.36 ms | 1.64x |
| `tables` | 52.59 ms | 30.73 ms | 1.71x |
| `takl` | 1.007 s | 583.77 ms | 1.73x |

The execution format now uses one-byte opcodes and two-byte binary operands.
The interpreter does not decode hexadecimal text in its hot loop.

GCC and Clang builds use computed-goto dispatch. Short non-allocating opcode
runs dispatch directly without rebuilding the current frame and code pointers.
`OT_COMPUTED_GOTO=0` selects the portable fallback from the same opcode
inventory.

Lexical references compile to packed depth-and-slot coordinates. Runtime
activation environments contain slot storage and do not allocate or scan a
binding object for every argument. Compiler environments retain names only
while producing those coordinates.

Binary `+`, `-`, `=`, and `<` applications compile to a guarded intrinsic
opcode when their standard bindings are visible. The opcode resolves the live
binding at runtime. If it has changed, the VM performs an ordinary call with
the original tail-call behavior. Other non-reentrant core natives run directly
against the traced VM operand stack instead of copying arguments into a rooted
C temporary array.

The normal semispace suite, a forced portable-dispatch suite, and the
generational-collector suite passed. Computed-goto and portable builds both
passed under GCC 15 and Clang. An AddressSanitizer and UndefinedBehavior
Sanitizer build also passed with leak detection disabled; LeakSanitizer itself
cannot run under the container's tracing setup. `make gc-lint` could not run
because the optional LLVM and Clang development headers are not installed.
