---
id: lan-515h
status: open
deps: []
links: [lan-5je1]
created: 2026-08-16T04:20:09Z
type: feature
priority: 2
assignee: Phil
parent: lan-qwff
tags: [tooling, editor, debug-info]
---
# Track definition locations for editor navigation

Conjure has a go-to-definition workflow, but Otium cannot currently report where a var was defined. Carry source locations far enough through reading and compilation to attach a definition location to vars, expose it through reflection, and wire the Conjure client to open it.

## Design

Use the same compact source-location representation planned for diagnostics rather than adding an editor-only metadata path. Record source name, line, and column for definitions loaded from files and evaluated buffers. Add a lookup operation that resolves unqualified, referred, and alias-qualified symbols in a requested namespace and returns the definition location when one exists. Builtins and generated definitions without a useful location should return no location cleanly. The Conjure client should implement its definition callback using this data.

## Acceptance Criteria

From an Otium buffer, the Conjure definition mapping opens local project definitions and loaded dependency definitions at the correct line and column. Unqualified, referred, and alias-qualified symbols resolve in the buffer namespace. Symbols without source metadata report that no definition is available. Runtime tests cover stored locations and namespace resolution, and a client test covers the Conjure response shape.

