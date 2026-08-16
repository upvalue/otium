---
id: lan-f999
status: closed
deps: []
links: []
created: 2026-08-16T00:37:22Z
type: chore
priority: 2
assignee: Phil
tags: [cleanup]
---
# Compress value.hpp Value constructors

Each inline constructor in value.hpp is ~6 lines of member assignment; collapse to one-liners via an aggregate-style helper or C++20 designated initializers. ~55 lines down to ~15, no behavior change.

