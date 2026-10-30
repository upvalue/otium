# Code quality review: Codex pass

Reviewed 2026-10-29. The original request authorized fixes, and this pass
edited source before the later review-only request arrived. No source was
edited while preparing this report. Locations below refer to the final
working tree; the problems describe behavior before our fixes. All numbered
findings below were fixed during the original pass.

The C sub-agent reviewed reader/tokenization, numbers, strings/bytes,
collections, filesystem, process/time, FFI preparation, ports, cleanup and
parameters. The library sub-agent reviewed core, extra, JSON, resource, time,
thread, actor, memory and SQLite. The parent reviewed development clients,
launchers and test/tooling infrastructure. This was not an exhaustive proof
of correctness, and we did not independently benchmark all these areas.

## 1. Memory statistics can wrap into negative or incorrect integers (bug)

- **Where:** `c/src/process.c:121` (`memory_counter`), `:126`
  (`memory_allocated`), `:134` (`memory_collections`), `:140` (`memory_heap_stat`).
- **Problem:** cumulative allocation and collection counters were converted
  directly to Otium integers. Allocation also added two counters without an
  overflow guard. The heap-stat clamp assumed a 63-bit integer on every target.
- **Why it matters:** diagnostic counters can become negative or misleading
  in long-lived processes; the hardcoded limit is wrong on 32-bit targets.
- **Fix:** share a saturating conversion using `INTPTR_MAX / 2`, and guard
  cumulative addition before conversion. Added direct counter-boundary tests
  in `c/tests/unit/process.c`.

## 2. Buffered threaded reads report the wrong UTF-8 error position (bug)

- **Where:** `c/src/ports.c:699`, `port_character`.
- **Problem:** invalid UTF-8 found during lookahead reported the port's
  current position without adding the lookahead distance.
- **Why it matters:** an error after valid input points at the beginning of
  the buffered read. For example, a threaded `read-all` of bytes 65, 66, 255
  reported position 0 instead of 2.
- **Fix:** report `port_object(*port)->position + ahead`. Added four threaded
  regressions for `read`, `read-all`, `read-line` and counted `read-string`,
  including a consumed prefix and a multibyte character.

## 3. Search callbacks can change the result or prevent termination (bug)

- **Where:** `lib/otium/core.ot:350` (`find`), `:370` (`find-index`).
- **Problem:** array searches compared the cursor with the collection's
  changing length. Shrinking past the cursor could prevent equality forever;
  appending kept extending the search. `find` also fetched the matched
  element again after calling the predicate, for both arrays and lists.
- **Why it matters:** a predicate that mutates its input can hang the search
  or cause it to return a value it never tested. Reproduced `find` returning
  9 after testing 1 and replacing that element with 9 inside the predicate.
- **Fix:** capture the initial array length and the value passed to the
  predicate. Subsequent element reads still see mutations; this is not a
  snapshot of the whole collection. Tests cover replacement, growth and
  shrinkage for both search functions.

## 4. `range` can wrap past its endpoint and keep running (bug)

- **Where:** `lib/otium/core.ot:429`.
- **Problem:** the loop unconditionally added its step after yielding each
  integer. Near an integer boundary, that addition wraps and the loop can
  continue on the opposite side of the number range.
- **Why it matters:** a range that should contain one or a few values can
  instead keep allocating elements until it exhausts memory.
- **Fix:** terminate when a positive step fails to increase the index, or
  a negative step fails to decrease it. This uses the runtime's wrapping
  integer arithmetic and has no word-size constants. Tests cover both
  boundaries, large steps and the most negative step, deriving bounds at
  runtime. Intermediate approaches using fixed bounds or assuming overflow
  promoted to float were discarded; neither is the final implementation.

## 5. `lcm` multiplies before canceling the GCD (bug)

- **Where:** `lib/otium/core.ot:1057`.
- **Problem:** it computed the product before dividing by the GCD, so an
  intermediate product could overflow even when the final LCM fits.
- **Why it matters:** representable exact results were wrong. For example,
  the largest positive fixnum and 3 have that fixnum as their LCM on the
  supported word widths, but the old expression overflowed first.
- **Fix:** divide the accumulated value by the GCD before multiplying.
  Regressions cover argument order, negative input, zero and no arguments.
  This does not add arbitrary precision or promise an exact result when the
  LCM itself exceeds the integer range.

## 6. Sorting long runs consumes a call frame per element (bug/perf)

- **Where:** `lib/otium/core.ot:510`, `merge-runs`.
- **Problem:** merging called itself underneath `cons`, so the merge was
  not tail recursive.
- **Why it matters:** sorting 30,000 elements exceeded the VM frame limit.
  Stack use grew linearly with run length even though mergesort's split
  recursion is only logarithmic.
- **Fix:** build the merged prefix through a sentinel and tail pointer,
  recurse in tail position, and attach the remaining run when one side is
  exhausted. Tests cover long ordered/reversed inputs, stability and lists.

## 7. Delayed actor messages silently drop `nil` (bug)

- **Where:** `lib/otium/actor.ot:69`, timer-loop's receive pattern.
- **Problem:** the timer request stored its payload under `:message`, but
  storing `nil` deletes a table key. The timer pattern required that key and
  therefore did not recognize a delayed `nil` message.
- **Why it matters:** `send-after` did not preserve the ordinary send API's
  payload semantics; the message was never delivered.
- **Fix:** make the pattern's message field optional with a `nil` default.
  Added delayed `nil` and `#f` cases to the actor tests.

## 8. The Python development reader hangs or mishandles malformed input (bug)

- **Where:** `tools/otium_tools/dev.py:44`, `_read`.
- **Problem:** an unexpected closing delimiter produced an empty symbol
  without advancing the cursor. Inside a collection, this looped forever.
  An escape at end of input raised IndexError; odd-length tables silently
  lost the last key; unknown escapes silently lost their backslash.
- **Why it matters:** malformed protocol data can hang a developer tool or
  produce misleading parsed data instead of a useful error.
- **Fix:** reject unexpected closing delimiters, dangling/unknown escapes,
  odd table entries and unsupported Python-dict keys with ValueError.
  The reader also recognizes Otium's `\e` escape. Regression cases run in
  a subprocess with a timeout so a reintroduced infinite loop is bounded.

## 9. Development requests use JSON escapes that Otium cannot read (bug)

- **Where:** `tools/otium_tools/dev.py:113`, `encode`; formerly also the
  separate sender now represented by `tools/otium_tools/client.py:23`.
- **Problem:** Python's JSON serializer emitted escapes such as `\b`, `\f`
  and `\u0000`, while Otium's reader accepts its own escape syntax.
- **Why it matters:** otherwise valid code or text containing control
  characters failed to reach the running program through the dev tools.
- **Fix:** encode strings using Otium escapes, including `\x...;` for other
  control characters. Test all ASCII controls, quotes, backslashes and
  non-ASCII characters through Python and the actual Otium reader/writer;
  also test an attach request containing literal controls.

## 10. Attach/MCP drops the last stdin line without a newline (bug)

- **Where:** `tools/otium_tools/client.py:118`, EOF handling in `main`.
- **Problem:** EOF returned or waited for outstanding requests without
  processing the remaining input buffer.
- **Why it matters:** a piped expression or JSON-RPC request without a final
  newline disappeared without execution.
- **Fix:** process a buffered final line before finishing, then exit when
  pending work is complete. Added attach and MCP regressions, including an
  immediately answered `tools/list` request.

## 11. Raw control commands report success when the server rejects them (bug)

- **Where:** `tools/otium_tools/dev_cli.py:186`, control-command branch.
- **Problem:** `--raw` printed a failed interrupt/continue/abort response,
  then returned success unconditionally.
- **Why it matters:** shell automation could treat a failed control action
  as successful merely because it requested machine-oriented output.
- **Fix:** derive the raw branch's exit status from the response's `ok`
  field, as eval/load already do. Tests cover all three control commands.

## 12. Two development connection implementations duplicate framing (cleanup)

- **Where:** `tools/otium_tools/client.py:23` and
  `tools/otium_tools/dev.py:131`.
- **Problem:** attach/MCP maintained another copy of socket creation,
  request construction, frame limits and response buffering. Its constructor
  also lacked the shared connection's cleanup on connect failure.
- **Why it matters:** protocol fixes had to be applied twice; the escaping
  defect in item 9 existed in both implementations.
- **Fix:** subclass the shared connection and retain only attach/MCP's
  outstanding-request bookkeeping. Existing dev integration tests and the
  new request regressions exercise the shared implementation.

## 13. The runtime-reference check no longer checks public functions (bug/cleanup)

- **Where:** `tools/tests/tests.py:63`.
- **Problem:** the prelude regex still recognized `define` rather than
  `def`, so most public functions escaped the docstring check. Its
  non-greedy argument match also stopped at the first optional parameter's
  closing parenthesis.
- **Why it matters:** a passing reference test gave false assurance that
  public prelude functions had source documentation.
- **Fix:** recognize current `def` syntax and full single-line signatures,
  excluding internal `%` names. Removed the blanket catalog-duplication
  assertion: the core legitimately wraps documented bootstrap APIs such as
  `read-string` and `read-file`. The repaired reference check passes.

## Validation and remaining limits

- Ran 159 non-GC language tests: 156 passed initially; the two boot-image
  comparisons and generated namespace-reference check passed after rebuilding
  boot images and regenerating the reference JSON. The final changed range
  implementation also passed its focused rerun.
- Passed 24 relevant C unit cases and threaded checks under real and seeded
  scheduling. Passed 23 new library assertions under native, VM with a
  constrained frame limit, and unoptimized native execution. Actor tests
  passed eight scheduler seeds and the real scheduler.
- GC lint on `ports.c` and `process.c`: 186 functions, zero warnings.
  This was a targeted run, not a claim that the entire runtime has zero
  warnings. The wrapper hit a sandbox `/dev/fd` restriction, so the already
  built analyzer was invoked directly with the LLVM resource directory and
  macOS SDK. No GC test suites were run.
- Targeted lint and `git diff --check` passed. Whole-file format checks still
  flag existing declare/record-helper formatting in core and an unrelated
  idle-timer check in the thread tests. We left those portions unchanged.
- Regenerating `docs/reference.json` also picked up pre-existing stale UI
  metadata (`synced-overlay`, `paint-cacheable?`, `wake!`), besides changed
  core source-line references. No prose documentation was edited.

## Files edited during the original pass

Yes, source files were edited, by the parent and its two sub-agents. No
commits were made. Our complete edit list is:

- `c/src/ports.c`
- `c/src/process.c`
- `c/tests/unit/process.c`
- `lib/otium/core.ot`
- `lib/otium/actor.ot`
- `tools/otium_tools/client.py`
- `tools/otium_tools/dev.py`
- `tools/otium_tools/dev_cli.py`
- `tools/tests/tests.py`
- `tests/dev/tests.py`
- `tests/library/tests.py`
- `tests/library/quality.ot` (new)
- `tests/threads/actors.ot`
- `tests/threads/checks.ot`
- `docs/reference.json` (generated metadata)

We also rebuilt ignored build artifacts. We did not edit the already dirty
`c/src/builtins.c`, `foreign.c`, `modules.c`, or `runtime.c`, or the concurrent
changes in `c/tests/unit/modules.c` and `namespaces.c`. README.md and AGENTS.md
were untouched. This follow-up only creates `scratch/cq-codex.md`.

## Comparison with Claude's findings

Read `scratch/cq-c-runtime.md` (items 1–21) and
`scratch/cq-core-lib.md` (items 1–24).

### Duplicates and related findings

There are no direct duplicates among my numbered findings and Claude's
numbered findings. Claude's library introduction explicitly excludes the
core functions being edited by this pass.

- My item 3 concerns search correctness under mutation; Claude library
  item 8 concerns `contains?` failing to short-circuit. Related sequence
  helpers, different defects.
- My item 6 replaces recursive merge construction; Claude library item 7
  concerns `for :collect` reversing a freshly built list. Both use tail
  construction, but affect separate algorithms and call sites.
- My item 7 concerns missing delayed payloads; Claude library item 12
  mentions duplicate timeout-condition construction in actor/thread. These
  are separate timer/actor issues.
- My item 12 is connection-protocol duplication in Python. Claude C items
  11–16 and library item 12 identify other duplicated implementations.
- My targeted zero-warning GC-lint result does not contradict Claude C
  item 9's seven warnings in other runtime files. I did not independently
  verify the claim that all seven are false positives.

### Corrections or qualifications to Claude's report

1. **Library item 8: the array fix in the displayed example retains a
   linear allocation.** The diagnosis is sound, but using `member` for arrays
   calls `drop` to return the matching array tail (`core.ot:935`). A match
   at index zero still copies the whole array. Use `find-index` directly for
   the array branch and test against `nil`; use `member` for lists. The prose
   already suggests this distinction, but the combined example does not.

2. **C item 7: the proposed fix is incomplete.** The verifier reads `o->gc`
   and `o->size` before the cited block-state access, and the failure-report
   path reads block metadata again (`heap.c:680–703`). Merely moving
   `oti_heap_contains` ahead of `heap_block_info(o)->state` does not make
   arbitrary invalid pointers safe. Validate address membership before any
   header dereference and keep the diagnostic path from dereferencing an
   unvalidated object. The reported underlying bug is valid.

3. **Library item 20: the unlocked-core folding claim needs a reproducer.**
   `procedure-of` does cache function values, but normal call conversion
   checks `%core-locked?` before selecting direct/known calls
   (`compiler/convert.ot:638`, `:645`), and inline's global-call fact folding
   and core-body inlining also check it (`inline.ot:280`, `:642`). Thus the
   cache alone does not establish that ordinary calls compiled under
   `--c.unlock-core` fold against stale functions. I would not present that
   subclaim as a confirmed bug without showing a call path that bypasses
   those guards. The separate variable-ID reset concern is plausible and
   was not reproduced in this pass.

4. **Library item 21: both proposed alternatives need semantic care.** A
   permanent cache of closable dispatch types must be invalidated when live
   generic methods change. Simply letting `close` fail later can execute the
   resource body before detecting a missing release method, changing the
   current pre-body check. The repeated scan is real; the alternatives are
   not automatically behavior-preserving.

I did not reproduce Claude's performance measurements or OOM paths, and do
not claim the remaining items have been independently validated. Aside from
these specific corrections/qualifications, I found no further basis in this
follow-up to label a reported finding wrong.
