---
id: lan-w4o0
status: open
deps: []
links: []
created: 2026-09-06T22:39:42Z
type: feature
priority: 2
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Support executable scripts with a shebang

The reader rejects every non-boolean token beginning with hash, so a conventional shebang on the first line makes a script fail before evaluation.

## Acceptance Criteria

The CLI accepts one shebang line only at the beginning of a source file without weakening the language reader contract for embedded or later hash tokens; tests cover direct execution and malformed placement.

