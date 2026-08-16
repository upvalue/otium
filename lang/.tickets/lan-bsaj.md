---
id: lan-bsaj
status: open
deps: []
links: []
created: 2026-08-16T04:19:59Z
type: feature
priority: 2
assignee: Phil
parent: lan-qwff
tags: [tooling, editor, repl]
---
# Add live Otium completion to the Conjure client

The Conjure client can evaluate code and describe a known symbol, but it has no Otium-aware completion. Add enough runtime reflection to enumerate the live namespace registry, then expose those results through the Conjure completion API.

## Design

Add small reflection builtins for listing namespaces and vars, resolving symbols, and reading var metadata. Keep the result as ordinary Otium data so the query behavior can be tested without Neovim. Complete unqualified names from the current namespace, refers, and core. Complete alias-qualified prefixes through namespace aliases, and do not expose private vars across namespaces. Return function parameters and documentation when available. Teach the client to use the namespace declared in the current buffer. Lexical locals remain out of scope; normal buffer completion can cover them. Candidates reflect the live VM and can be stale until the defining buffer is evaluated.

## Acceptance Criteria

In an Otium buffer connected to Conjure, omnifunc and cmp-conjure return live public vars from the current and loaded namespaces. Alias-qualified prefixes work. Completion entries include signatures and documentation when metadata exists. Documentation lookup uses the buffer namespace. Runtime tests cover reflection and visibility, and client tests cover candidate formatting.

