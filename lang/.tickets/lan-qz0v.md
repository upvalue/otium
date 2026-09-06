---
id: lan-qz0v
status: open
deps: []
links: []
created: 2026-09-06T22:39:42Z
type: bug
priority: 1
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Parse project files structurally and report explicit failures

Project loading extracts every quoted substring without parsing comments, escapes, or the paths form, and a missing explicitly requested project file is silently ignored. The host duplicates a fragile subset of the language reader.

## Acceptance Criteria

Project files are parsed through a structural parser or a safely reused reader; only valid path entries are accepted; comments and escaped strings behave correctly; an explicit unreadable or malformed project produces a clear nonzero CLI error.

