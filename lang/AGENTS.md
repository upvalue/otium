# otium

Otium is an experimental programming language. A small Lisp intended to be
malleable and intended for running in low memory environments, but with some
big-language features and ideas. Most directly Scheme like in heritage but
takes some ideas from Clojure (namespaces) and Janet (practicality, tables).

Use `ticket --help` to interact with the ticketing system.

Never edit README.md or AGENTS.md.

# Guide

- Live reloading is important
- Performance is important
- The Chez Scheme implementation is the primary implementation; the C
  implementation is experimental and can lag behind.
- However it is ideal to extend the higher-level language to meet challenges e.g. 
  when something can be done in Otium over using escape hatches to the runtime,
  it should be.

# GC Development

When changing C code around allocation, roots, or managed object pointers, run
`make gc-lint` if the optional LLVM development tools are installed. This is
not part of the default build or test targets. The runner is
`c/tools/gc-lint/run`; it discovers Homebrew LLVM or uses `LLVM_CONFIG` when
supplied.

Avoid running GC tests unless directly working on the C garbage collector!

## Runtime layout

`c/` owns the C runtime, build, tests, and implementation notes.
`chez/` owns Otium on Chez Scheme, including its FFI adapter and tests.
`shared/` owns runtime-independent native components and their dependencies.
`examples/` holds runnable examples for both runtimes, organized by subject.
Native extension fixtures stay with their runtime tests.
`benchmarks/` owns workloads, runners, and results; `benchmarks/workloads/`
runs on both runtimes, and `benchmarks/r7rs/` holds Otium ports plus the upstream
Scheme suite. Runtime-specific diagnostics live beside their benchmark family.
The native collector harness stays in `c/benchmarks/`. Track non-FFI
portability blockers in `GAPS.txt`; runtime-specific placement is not a goal.
`docs/` holds language-level documentation. Chez uses Chez values and semantics
and is not required to pass the C dialect conformance suite. Root `make test`
tests both runtimes and their shared REPL. Use `make test-c`, `make test-chez`,
`make test-repl`, or `make test-sdl` for focused checks.
