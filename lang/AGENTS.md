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

## Runtime layout

`c/` owns the C runtime, build, tests, examples, and implementation notes.
`chez/` owns Otium on Chez Scheme, including SDL3 demos and its tests.
`benchmarks/` holds comparisons between runtimes. Keep runtime-specific work
inside its directory; Chez uses Chez values and semantics and is not required
to pass the C dialect conformance suite. Root `make test` tests both runtimes.
Use `make test-c`, `make test-chez`, or `make test-sdl` for focused checks.
