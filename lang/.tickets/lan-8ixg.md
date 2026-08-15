---
id: lan-8ixg
status: closed
deps: []
links: [lan-6mpt]
created: 2026-08-15T23:31:53Z
type: chore
priority: 2
assignee: Phil
---
# Adopt clang-tidy with a curated check set

Add linting without buying the full modern-C++ package, which would fight the codebase on purpose-chosen grounds (no exceptions, no STL, manual malloc/free, placement-new Buf, the Heap-is-first-member reinterpret_cast).

Checked-in .clang-tidy enabling clang-analyzer-*, bugprone-*, misc-unused-*, and selective performance-*; modernize-* and cppcoreguidelines-* disabled wholesale. Warnings-as-errors for the enabled set. meson provides a clang-tidy target automatically once the config file exists.

Also the cheaper compiler-flag wins: add -Wshadow (would have flagged near-misses in the GC cursor rewrites), clean up the two current unused warnings (data.cpp array_push vm param, string.cpp utf8_len), then werror=true.

Note: none of this catches the GC staleness class -- a stale Value is a legal POD copy to the analyzer. That's lan-6mpt's job (structural, via handles + a grep lint for raw Value locals); a custom gcmole-style clang check is the fallback only if we keep the raw-Value API long-term.

## Acceptance Criteria

.clang-tidy checked in; ninja clang-tidy clean; werror=true builds green on clang


## Notes

**2026-08-15T23:42:54Z**

Added curated .clang-tidy, enabled -Wshadow and werror, removed the two existing unused warnings, and fixed analyzer findings. Verified clang-tidy clean with LLVM 22.1.8; Clang warning-as-error build and both Meson tests pass.
