# Prototype: closing the gaps with the C runtime

A comparison of `prototype/` and `c/` found that the prototype has the better
overall design. Much of its local code quality has since caught up: object
kinds have structs, builtins have per-module tables, and the tests are one
runner. Three gaps remain where `c/` is still better. Work through them in this
order:

1. A rooting checker.
2. A structured error model.
3. The smaller items under "Later".

The broader embedding API and a generational collector are out of scope for
now.

## Ground rules

Work on `wip-runtime-rewrite`. Other sessions commit to it, including VM work
in `vm.c` and `compiler.c`. Check `git log` and `git status` before starting,
and again before committing. Don't blanket-stage. Keep `vm.c` and `compiler.c`
edits small and in their own commits.

Read `AGENTS.md`, `AGENT-LANG.md`, `docs/runtime-rewrite.md` and
`docs/vm-design.md` first. Never edit README.md or AGENTS.md. The clean-room
rule in `prototype-completion.md` still applies: don't read `c/src` or
repository history for implementation ideas. `c/tools/gc-lint` is tooling, not
the runtime, so it's fine to read it for its approach.

Run these after each step:

- `make -C prototype test sanitize test-switch check-public test-no-compiler`.
  All 114 tests pass at the time of writing.
- `make -C prototype compliance`. The baseline is 67 groups passing and one
  failing (`diagnostic output`).
- `make -C prototype differential` and the `differential_*.py` scripts. The
  baseline is 74 programs and 50 errors matching Chez. `differential_cleanup.py`
  already fails on the baseline. `differential_foreign.py` needs Chez's
  `build/libotium-ffi.so` at the repo root.
- `bench_runtime` through `benchmark_compare.py` against the previous build.
  Neither step should slow release builds.

## 1. Rooting checker

The collector in `heap.c` is a moving semispace collector. Roots are explicit:
`oti_roots_push` and `oti_roots_pop` around arrays of `ot_value`. The only check
is an assert that pops happen in LIFO order. `ot_gc_stress` collects on every
allocation, but the old semispace is left intact. A stale pointer still reads
the old object, so a rooting bug can pass every test by luck.

The bugs to catch:

- An `ot_value` local that isn't rooted across a call that can allocate, collect
  or re-enter the VM.
- An `oti_*_obj*` pointer from an `oti_as_*` cast that is held across such a
  call.
- A pointer into `rt->stack` held across `oti_stack` growth, which reallocates
  the stack.

Do it in two layers.

**Dynamic, first.** In check builds, make stale pointers fail loudly:

- Poison the old space after each collection, or replace it with fresh memory
  and free or `mprotect` the old space.
- Move the stack on every growth.
- Run the whole suite with collection forced at every allocation.

Add a make target for this and document it in the Checks section of
`docs/runtime-rewrite.md`. Fix every bug it finds, with a regression test where
one is practical. The kind checks in sanitizer builds (`OTIUM_CHECK_KINDS`)
catch wrong casts, but not missing roots.

**Static, second, optional.** Annotate the functions that may allocate and add
a clang-based check in the spirit of `c/tools/gc-lint`. The LLVM development
tools aren't installed here (there is no `llvm-config`), so this must stay
optional, like `make gc-lint`, and never become a build dependency.

Done means:

- The target exists and is documented.
- The full suite passes under it.
- Every bug it found is fixed.
- Release performance is unchanged.

## 2. Error model

Errors are a fixed `char error[192]` in `ot_state`, plus `error_code`, `raised`,
`signaled` and `condition_values`. `oti_error`, `oti_fail` and
`oti_coded_error` record them, and `oti_error_condition` later turns them into
conditions. The problems:

- Messages over 192 bytes are truncated.
- Structure is recovered by parsing the string. `foreign.c` parses
  `"FFI <reason>: ..."`, and `modules.c` checks for a `"module ["` prefix.
- `cleanup.c`, `conditions.c`, `ports.c` and `modules.c` save and restore the
  error with 192-byte `memcpy` copies.
- `vm.c` and `compiler.c` test `rt->error[0]` in 13 places.

The goal is a structured, untruncated error record with no parsing. Error sites
must not allocate on the managed heap. Callers often hold unrooted values and
return straight after recording an error, so keep the record host-side and
build the condition at the catch point, as now. One possible shape:

- A record with a kind, a code, an owned message and typed fields for the cases
  that are parsed today, such as the FFI reason and operation, or the module
  name.
- A printf-style `oti_errorf`.
- An `oti_failed(rt)` predicate in place of `rt->error[0]`.
- Save and restore as a struct move.

Keep the message text the same wherever tests compare it, both in the
differential checks against Chez and in the compliance diagnostics. Once this
lands, look at the failing `diagnostic output` compliance group. It may be
easier to fix with error codes carried as data.

## Later

Not part of this pass. Listed so they aren't lost:

- Move function code and constants onto the GC heap, which would remove the
  separate marking in `reclaim.c`. This is mostly VM work.
- Delete the five builtins that `core.ot` already replaces: `contains?`,
  `condition-type`, `condition-message`, `error?` and `digit-value`. Then work
  through `docs/prototype-otium-candidates.md`.
- Convert `vm.c` and `compiler.c` from `object_fields` to the object structs
  once the VM work settles.
- `ports.c` uses a `PORT_LIVE` macro to generate 31 builtin wrappers that grep
  can't find by name. Consider writing them as plain functions.
