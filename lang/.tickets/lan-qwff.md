---
id: lan-qwff
status: in_progress
deps: []
links: [lan-q4lf]
created: 2026-08-16T01:37:50Z
type: feature
priority: 2
assignee: Phil
tags: [tooling, editor, repl]
---
# Interactive programming support (neovim/Conjure)

Editor-integrated interactive programming for otium, neovim-first, using the live-VM model (Conjure/CIDER style) rather than static analysis — macros are closures evaluated at expansion time, so a live VM is the only reliable source of truth.

Phased plan (from design discussion 2026-08-15):

Phase 0 — editor basics (no runtime changes):
- Vim syntax file or tree-sitter grammar. Keyword lists are ready-made: special forms hardcoded in Vm::syms (src/vm.hpp:38), 122 builtins.
- Decide file extension: .scm currently collides with Scheme ftdetect (mostly-workable interim: Scheme highlighting + vim-sexp/parinfer already work).

Phase 1a — send-to-REPL (works today): vim-slime or iron.nvim into a terminal REPL. Multi-line piped input already handled via reader incomplete flag (repl/main.cpp:271).

Phase 1b — Conjure client + runtime support:
- Reflection builtins: all-ns, ns-vars, var-meta/resolve. Data all exists in the ns registry (src/ns.cpp); ~100 lines of def_native. Enables completion/hover/doc to be written in otium itself.
- Machine-readable server mode: socket or stdio with delimited output (results vs program stdout vs errors). Reuse EvalSourcePolicy/EvalSourceState (src/eval.hpp:26).
- Restart prompt handling: interactive handler (repl/main.cpp:109) blocks on fgets(stdin) and would deadlock a client — surface restarts through the protocol or auto-abort in server mode.
- Fennel Conjure client: eval-at-point, doc lookup (VAR_DOC), macroexpand-at-point (builtins exist, src/builtins/sys.cpp:259), completions fn wired to cmp-conjure. Alias-qualified prefixes (foo/ba) resolve through ns :aliases, filter on VAR_PRIVATE; show FunctionData::params as signature. Known limits: locals don't complete (lexical frames not enumerable — mitigate with cmp buffer source); candidates are stale until buffer is evaluated.

Phase 2 — LSP proper (deferred): thin adapter over the live VM. Go-to-definition and precise diagnostics are blocked on source locations (no parsed form carries one; Value has no metadata slot) and structured errors (positions are sprintf'd into messages, src/vm.cpp:165). Do the source-location work as part of the bytecode compiler rewrite (lan-q4lf) — a compiler wants a line table anyway; don't thread it through the tree-walker twice.

## Acceptance Criteria

Phase 0: otium files get correct highlighting and structural editing in neovim. Phase 1b: from a neovim buffer, can eval form at point, see docs on a symbol, macroexpand at point, and get live completions (with signatures/docs) via cmp-conjure; server mode never deadlocks on the restart prompt.


## Notes

**2026-08-16T02:38:08Z**

Phase 1b first slice: added a framed --server stdio mode and an Otium Conjure client with live eval, docs, macroexpand, start/stop, and interrupt support. Live completion/reflection remains.
