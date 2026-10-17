# otium

Otium is an experimental programming language. A small Lisp intended to be
malleable and intended for running in low memory environments, but with some
big-language features and ideas. Most directly Scheme like in heritage but
takes some ideas from Clojure (namespaces) and Janet (practicality, tables).

Use `ticket --help` to interact with the ticketing system.

Never edit README.md, it's for humans only.

When changing C code around allocation, roots, or managed object pointers, run
`make gc-lint` if the optional LLVM development tools are installed. This is
not part of the default build or test targets. See `c/agent-docs/gc-lint.md` for
setup, current coverage, and escape hatches.

# Principles

- Live reloading is important
- Performance is important
- Supporting both the C and Chez runtimes is important; a design should try to
  minimize unexpected behavioral differences and introduce abstractions that
  smooth them where possible

## Runtime layout

`c/` owns the C runtime, build, tests, and implementation notes.
`chez/` owns Otium on Chez Scheme, including its FFI adapter and tests.
`shared/` owns runtime-independent native components and their dependencies.
`examples/c/` and `examples/chez/` hold examples for each dialect.
`benchmarks/` owns workloads, runners, and results; `benchmarks/c/` targets C,
and `benchmarks/r7rs/` holds C-dialect ports plus the upstream Scheme suite.
The native collector harness stays in `c/benchmarks/`. Track non-FFI
portability blockers in `GAPS.txt`; runtime-specific placement is not a goal.
`docs/` holds language-level documentation. Chez uses Chez values and semantics
and is not required to pass the C dialect conformance suite. Root `make test`
tests both runtimes and their shared REPL. Use `make test-c`, `make test-chez`,
`make test-repl`, or `make test-sdl` for focused checks.
