---
id: lan-jjq5
status: open
deps: []
links: []
created: 2026-08-16T00:36:33Z
type: chore
priority: 2
assignee: Phil
tags: [runtime, cleanup]
---
# Choose one implementation path for empty? and get-in

Normal Meson builds register native implementations of empty? and get-in, then the embedded prelude defines both names again and replaces the native functions. That leaves two implementations of each operation with no test requiring them to stay equivalent. Decide whether a runtime without the embedded prelude is supported, then keep only the implementation path that decision requires.

## Design

If the embedded prelude is required, remove the native implementations and registrations. If a prelude-free runtime is supported, make the fallback explicit rather than registering code that is silently overwritten, and test both paths for the same behavior.

## Acceptance Criteria

The supported bootstrap modes are documented. Normal startup exposes the same empty? and get-in behavior as today. Duplicate implementations are removed or isolated behind an explicit prelude-free build path. Tests cover every supported path.

