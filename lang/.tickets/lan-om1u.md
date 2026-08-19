---
id: lan-om1u
status: closed
deps: []
links: []
created: 2026-08-16T02:49:28Z
type: feature
priority: 2
assignee: Phil
tags: [language, control-flow]
---
# Add named let syntax

Support Scheme-style named let forms such as `(let loop ((i 0)) body...)`, including the zero-binding form `(let loop () #t)`. A named let introduces a local recursive function and immediately invokes it with the binding initializers.

## Design

Lower a named let to an equivalent local function definition plus initial call, while preserving Otium lexical scoping and tail-call behavior. Specify whether initializer visibility follows Otium sequential `let` semantics or Scheme named-let semantics, and update the expander so local names correctly shadow macros.

## Acceptance Criteria

1. `(let loop () #t)` evaluates to `#t`. 2. Named lets accept zero or more bindings and can reference the loop name recursively. 3. Tail-recursive named lets run in constant stack space. 4. The loop name is scoped only to the named-let body. 5. Invalid forms produce an appropriate `let` error. 6. Evaluator, expander, and language-spec tests cover syntax, scoping, initializer semantics, and tail recursion; the spec documents the construct.

