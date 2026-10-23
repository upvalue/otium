---
id: lan-cck8
status: open
deps: []
links: [lan-giys]
created: 2026-10-21T00:47:25Z
type: task
priority: 1
assignee: Phil
tags: [ffi, performance, manse]
---
# Reduce FFI call overhead

Otium FFI calls cost ~175 ns on Chez against ~3 ns for a raw Chez foreign-procedure, and ffi/ref of a u64 costs ~132 ns (macOS arm64, 2026-10-20). manse walks libghostty-vt's render state with several FFI calls per cell: a full 80x24 walk with colours and style takes ~5.6 ms, so a 200x60 full-screen terminal would take ~35 ms per frame before drawing. Goal: a terminal that stays fast on big screens and with several terminals drawn on one screen at once. We don't need to match Ghostty itself.

Known suspects:
- The FFI callbacks work (commit cf8f5ed) made every bound call __collect_safe (chez/otium/ffi.sls ~245) so a callback can collect. That deactivates and reactivates the thread around every call and is likely a large part of the cost. Measure it alone. Options include marking only bindings that can call back, or a binding-time option; that would be a contract change, so bring it to Phil before deciding.
- The %bind wrapper: parameterize for error context per call and per argument, dynamic-wind even with no temporaries, map/iota over arguments, and encode recomputing expt and calling the native %sizeof helper for every integer argument.
- ffi/ref and ffi/field-ref resolve type descriptions and field paths on every call.

Benchmark to reproduce (Chez):
  (require otium.ffi :as ffi) (require chez)
  (def libc (ffi/open-library '("/usr/lib/libSystem.B.dylib")))
  (def abs-ffi (ffi/bind libc "abs" '(int) 'int))
  (def abs-raw (chez/foreign-procedure "abs" (int) int))
  time 200000 calls of each, and of (ffi/ref p 'u64 0).
The manse walk: manse/tests/ghostty.ot shows usage; write a 80x24 walk with manse.ghostty/each-cell and manse.ghostty/cell.

## Design

Measure before changing: attribute the 175 ns to its parts (collect_safe, wrapper, encode) and record the breakdown in a ticket note. Keep the FFI contract (errors, checks, error context data) unless Phil agrees otherwise; move work to bind time and to the error path instead. The same applies to the C runtime where it is cheap, but Chez is the priority.

## Acceptance Criteria

Scalar FFI calls and ffi/ref within a small multiple of raw Chez (target: under 20 ns for abs). The manse 80x24 full-cell walk measured before and after. Add an FFI call benchmark under benchmarks/. FFI tests pass on both runtimes (just test c chez shared).


## Notes

**2026-10-21T01:05:01Z**

Breakdown, Chez 10.4.1, macOS arm64. Bound abs was 157 ns: __collect_safe ~36, wrapper ~118 (parameterize per call ~17 and per argument ~22, map/iota/apply ~15, native %sizeof and offi_signed helpers ~7 each, dynamic-wind ~3). After e859df6: 43 ns, of which raw collect-safe abs is 40. ffi/ref u64 was 130: Otium type lookup (%scalar-id walk plus two native helper calls), and safe foreign-ref checking its address out of line (~20 ns). Now 52 ns, of which ~31 is type resolution in lib/otium/ffi.ot. field-ref 56-74 ns, ~40 of it path resolution through small Otium helpers. Manse 80x24 cell walk: 5.65 -> 2.6 ms. Per cell ~1.26 us: 9 field-refs ~600 ns, the 6-key result table ~270 ns, ~5 bound calls ~215 ns, of which collect-safe ~180. Under 20 ns for abs needs dropping __collect_safe per binding: plain calls deadlock if a callback arrives on another thread and collects (checked with call_on_thread). Waiting on Phil.

**2026-10-21T01:10:08Z**

Decision (Phil): every binding states collect-safety explicitly, true or false; no default. Not started. Callers to update: lib/otium/ffi.ot (bind, defbindings), lib/otium/sdl.ot, lib/otium/ui/output.ot, lib/otium/ui/native.ot, native/sdl/bindings.ot, native/sdl/reload.ot, native/sdl/host.py, tests/ffi/*.ot (common.ot has 36), tests/ui/policy-scene.ot, tests/ui/events.ot, tests/sdl/common.ot, examples/math.ot, benchmarks/workloads/ffi.ot, specs/ffi.md. manse/ghostty.ot and manse/pty.ot belong to the manse session. Open: argument position or keyword; what false means on the C runtime.

**2026-10-21T01:22:38Z**

Implemented required fifth collect-safe? boolean in bind and defbindings, with strict validation on both runtimes. Chez selects the foreign-call convention at bind time and caches each mode separately; C accepts both modes with the same call path. Migrated bindings and documented blocking calls, same-thread callbacks, and the wrong-thread deadlock caveat. Fixed partial-edit callback cache and cddddr errors. All 10 FFI tests pass (217 fixture checks and 51 callback checks per runtime); 113 selected shared/Chez regression tests pass after rerunning 24 socket tests outside the sandbox. No GC suites run. Chez abs timing including loop overhead: #f 7.6-7.8 ns, #t 41.6-43.4 ns. Benchmark workload passes both runtimes. PTY test passes. Fresh Ghostty walk timing blocked by unrelated manse.json character/string mismatch after the I/O merge; json/parse of {} reproduces without FFI. Changes remain uncommitted.
