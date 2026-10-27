# otium

Otium is an experimental programming language. A small Lisp intended to be
malleable and intended for running in low memory environments, but with some
big-language features and ideas. Most directly Scheme like in heritage but
takes some ideas from Clojure (namespaces) and Janet (practicality, tables).

Use `ticket --help` to interact with the ticketing system.

Never edit README.md or AGENTS.md. 

Read AGENT-LANG.md before writing Otium code.

# Guide

- Live reloading is important
- Performance is important
- The Chez Scheme implementation is the primary implementation; the C
  implementation is experimental and can lag behind.
- However it is ideal to extend the higher-level language to meet challenges e.g. 
  when something can be done in Otium over using escape hatches to the runtime,
  it should be.

# Repository

- docs/ is managed by humans; while some of the docs within may be LLM
  generated, you should never directly put documents in there without instructions.

# Manse

Manse is a development environment written in Otium and powered by Ghostty.

# GC Development

When changing C code around allocation, roots, or managed object pointers, run
`make gc-lint` if the optional LLVM development tools are installed. This is
not part of the default build or test targets. The runner is
`c/tools/gc-lint/run`; it discovers Homebrew LLVM or uses `LLVM_CONFIG` when
supplied.

Avoid running GC tests unless directly working on the C garbage collector!

