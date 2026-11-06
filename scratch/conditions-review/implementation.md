# Failure propagation repairs — 2026-11-06

Working branch: `conditions-review`. Rebased the research commit onto local main
`397247cc5036e0f80e761c7763a7674c66999f4f`; rebased research commit is
`417798db38218e254f01f8d475765e57f45a4d94`. All three defects still reproduced
before implementation. The existing `updot/aegis` delivery ref remains at
`101e99a79dae253cd87df59eb529ac7e2cdbc2b9`.

- Actor replies now carry `:failed? #t` separately from `:error`. Missing payload
  means raised nil when the flag is present. Error replies require this explicit
  flag; there is no legacy error-reply fallback.
- FFI state has a separate `pending_condition` boolean. The existing rooted payload
  slot still holds the condition. First failure wins even when it is nil; nested
  calls defer propagation until the outermost foreign call returns. No changes to
  foreign-frame unwinding or public condition representation.
- Group child bodies now catch all escaping conditions, record the group failure,
  and rethrow. Handled child errors, successful nil, kills, exits, and recovered
  restarts are not group failures. Child cleanup errors follow existing replacement
  semantics. Linked siblings unwind and independent groups retain later failures.

## Verification

Separate headless build: `/tmp/otium-conditions-review-build`, `-Dui=disabled`,
`--wrap-mode=nofallback`. It retains `-Dwerror=false` because GCC 16.2 reports the
pre-existing maybe-uninitialized warning at `c/src/builtins.c:168-177`.
`meson compile -C /tmp/otium-conditions-review-build -j 4` succeeded.

With `OTIUM_BUILD=/tmp/otium-conditions-review-build`, `tests/run` passed these 16
named groups against the final implementation:

- failure boundaries (54 checks for each of eight scheduler seeds)
- failure boundaries real scheduler (54 checks)
- thread group exit (child and owner cleanup, exit status 7)
- ffi failure payloads (28 checks)
- ffi failure payloads vm (28 checks, explicit `--c.vm`)
- ffi callbacks (existing callback/restart semantics)
- ffi callback exit
- conditions
- conditions cleanup error
- conditions exit
- conditions quit
- conditions resource exit
- threads (eight scheduler seeds)
- actors (eight scheduler seeds)
- threads exit
- threads failure report

New payload matrices include nil, false, integer, symbol, table, and record;
actor/group cases also include :quit. Tests check successful follow-up work,
nested actor and FFI boundaries, first callback failure retention, C completing
all callbacks before error propagation, callback-local recovery, cleanup,
linked cancellation, independent aggregation, and group restart recovery.

`c/tools/gc-lint/run c/src/foreign.c` passed: 191 functions, zero warnings.
Targeted Otium lint with `--deny-warnings`, Otium formatter checks,
`clang-format --dry-run --Werror c/src/foreign.c`, and `git diff --check` passed.
No GC stress tests, GUI/live REPL interaction, ARM tests, or full-suite claim.

## Diagnostic limitation

Group catch/rethrow now goes through the existing caught-error path, which clears
original source metadata. Uncaught group child table errors can consequently
report `lib/otium/thread.ot`'s `(error condition)` line rather than their original
site. Payload identity and failure propagation are preserved. This is observed,
not hypothetical. Preserving caught-error source metadata is the separately
identified diagnostic work; this change intentionally does not redesign traces.

All changes are confined to this worktree. Main HEAD and index checksum were
unchanged at verification. Existing main untracked ticket/scratch files were left
alone. No delivery ref moved, merge, push, or worktree removal was performed.
