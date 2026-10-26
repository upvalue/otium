# Prototype cleanup brief

Clean up the VM rewrite in `prototype/` so it is readable, consistently formatted,
and straightforward to extend. Preserve behavior. Feature completion is described
in [prototype-completion.md](prototype-completion.md).

## Starting state

The branch is `wip-runtime-rewrite`. The initial checkpoint is `26bd86b`
(`WIP: checkpoint clean-room C runtime rewrite`). Substantial implementation work
since that checkpoint is uncommitted, including new source files and tests.
Inspect the current worktree before editing or committing. The runtime goal was
paused when these briefs were requested; this document does not resume it.

Execution uses the bytecode compiler and VM. The generated-C execution tier was
removed. `prototype/compiler.c` is the bytecode compiler and must remain.
`prototype/modules.c` is the module loader and must also remain. Deleted
`module.c`, `native.c`, `native_numeric.h` and native-tier tests belong to the
retired generated-C backend.

Read `docs/runtime-rewrite.md` and `.tickets/oti-6ybd.md`. The design log records
successive milestones: older sections describe superseded behavior. Its latest
entries and the source are the best starting points.

## Constraints

- Never inspect the original `c/src` implementation or repository history. This
  is a clean-room rewrite. Public specifications, libraries, examples, tests,
  test fixtures and Chez behavior are available as references.
- Never edit README.md or AGENTS.md. Read AGENT-LANG.md before writing Otium.
- Keep higher-level behavior in shared Otium where practical.
- Preserve unrelated work. At handoff this includes `.claude/skills`, `.stignore`,
  `benchmarks/r7rs/`, `chez/run`, `compiler-plan.md`, `os-revisions.md`,
  `threading-plan.md`, and `examples/hand/in-15-minutes.ot`. Do not blanket-stage
  or revert the worktree. Recheck ownership if the tree has changed.
- Do not restore the generated-C backend or introduce compiler subprocesses.

## Work

1. Establish a baseline. Run the prototype tests and core compliance inventory
   below. Record failures before changing formatting or structure.
2. Format prototype C sources and headers with the repository `.clang-format`.
   Check the available clang-format version and record it. Use an explicit file
   list limited to `prototype/`; do not format the original runtime or unrelated
   files. Keep formatting separate from semantic refactoring in reviewable
   commits or patches. Check formatting idempotence.
3. Review module boundaries and simplify code where it makes ownership and
   control flow easier to follow. `foreign.c` now includes loading, ABI trees,
   signature caching, calls, native memory, layout snapshots and conditions;
   it is a strong candidate for decomposition. Review `builtins.c` and
   `internal.h` together: builtin IDs, name arrays and dispatch ranges must stay
   synchronized. Keep the public embedding API separate from internal helpers.
4. Expand compressed code into conventional blocks. Consolidate repeated
   arity/range checks, cleanup paths and test helpers where semantics agree.
   Avoid a generic framework merely to reduce line count. Name helpers around
   the operation or invariant they implement.
5. Audit allocation and ownership boundaries while refactoring. Managed values
   must be rooted across allocation, and interior pointers must be reloaded
   afterwards. Never register the same root slot twice. Native metadata must
   remain live through GC and callback reentry. In particular, publish list
   links after allocating when GC may reclaim the old list head. Preserve
   rollback behavior and cleanup on partial failure.
6. Simplify build/test maintenance without dropping coverage. Ensure new source
   files participate in normal and sanitizer builds. The default `sanitize`
   target is not exhaustive; inspect individual sanitizer targets. Remove
   stale generated-C references from active commands, while retaining clearly
   labelled historical measurements. Do not delete untracked source as debris.
7. Reconcile `docs/runtime-rewrite.md`: make current architecture and limitations
   easy to find, and clearly separate historical milestones. Update the ticket
   with actual changes and validation. Avoid claims that all FFI tests pass.

## Validation

Run from the repository root:

```sh
make -C prototype -j2 test
python3 prototype/compliance.py --output /tmp/otium-cleanup-compliance.json
make -C prototype test-foreign test-foreign-reload
ASAN_OPTIONS=detect_leaks=0 make -C prototype sanitize-foreign
git diff --check
```

Run relevant differential and sanitizer targets for each substantive refactor.
The last recorded core inventory is 49 passing groups. The expanded foreign
regression includes 23 bulk checks and 39 aggregate/error checks, plus cache and
lifetime tests. The complete common FFI suite still stops at unions.

Leak checking was disabled because of the tracing environment; do not describe
that run as leak validation. If changing allocation, roots or managed pointers,
run `make gc-lint` when the optional LLVM tools are installed. They were
unavailable during this session. Do not run the original C GC test suite; the
prototype's own collector tests are relevant to changes in this runtime.

## Done when

Formatting is consistent and repeatable, factoring makes the runtime easier to
extend, ownership/control-transfer invariants remain intact, and the baseline
and affected checks pass. Report remaining structural concerns with concrete
locations. Do not turn this cleanup into a feature rewrite or declare the VM
completion goal finished on the strength of cleanup tests.
