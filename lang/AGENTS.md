# otium

Otium is an experimental programming language. A small Lisp intended to be
malleable and intended for running in low memory environments, but with some
big-language features and ideas. Most directly Scheme like in heritage but
takes some ideas from Clojure (namespaces) and Janet (practicality, tables).

Use `ticket --help` to interact with the ticketing system.

When changing C code around allocation, roots, or managed object pointers, run
`make gc-lint` if the optional LLVM development tools are installed. This is
not part of the default build or test targets. See `agent-docs/gc-lint.md` for
setup, current coverage, and escape hatches.
