---
id: oti-0m9l
status: open
deps: []
links: []
created: 2026-10-27T03:28:29Z
type: task
priority: 3
assignee: Phil
tags: [prototype, performance]
---
# Call functions with optional parameters without leaving the VM

CALL and TAIL_CALL have an in-VM fast path only for closures called with exactly their arity and no optional or rest parameters (fn->variadic false). Everything else goes through oti_apply and oti_enter in C.

Optional parameters could take the fast path: accept arity <= argc <= arity + optional, set the missing parameter slots to unbound, and push the frame. The callee's JBOUND instructions already compute the defaults. Rest parameters need a list allocated, so they gain less and can stay on the slow path.

On the R7RS ports oti_enter is 2.4% of nqueens' time and under 1% elsewhere, so the ceiling there is small. Code that uses &optional helpers heavily, such as Manse and the libraries, may gain more, so measure it there.

## Acceptance Criteria

Calls to functions with &optional parameters and no rest parameter don't go through oti_enter. Defaults still evaluate correctly, including when an optional parameter is captured or boxed. Tests pass in release, sanitize and switch builds.

