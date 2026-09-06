---
id: lan-sdgp
status: open
deps: []
links: []
created: 2026-09-06T22:39:01Z
type: bug
priority: 2
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Remove or implement the ghost stack-slots CLI option

The CLI accepts and tests stack-slots, but does not document it and discards the parsed value. Users receive successful validation with no effect.

## Acceptance Criteria

The option is either removed everywhere or wired to a documented runtime setting; help text, parser behavior, and CLI tests agree.

