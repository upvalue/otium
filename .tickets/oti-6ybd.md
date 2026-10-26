---
id: oti-6ybd
status: in_progress
deps: []
links: []
created: 2026-10-26T06:58:01Z
type: epic
priority: 1
assignee: Phil
---
# Build clean-room C runtime and optimizing compiler

Independent implementation in prototype/. Use language documentation, standard library, tests and examples as semantic evidence; do not inspect original C runtime or repository history. Deliver precise compacting GC, compiler, basic FFI and measured performance against naive VMs and Scheme.


## Notes

**2026-10-26T07:00:25Z**

Added independent prototype/ heap: tagged values, explicit roots, fixed-budget Cheney semispace collector. GCC and Clang warning-clean tests pass, as do ASan/UBSan with leak detection disabled because sandbox /proc access prevents LSan. DESIGN.md records compiler, FFI and benchmark work still required. No original C runtime or repository history consulted.

**2026-10-26T07:07:22Z**

Added independent bounded-arena C reader with Unicode characters and escapes, numbers, strings, dotted lists, constructor syntax and quote prefixes. Validates decoded values for all 58 public positive reader fixtures and rejects all 55 negatives. Parses 67 library/example/workload files. GCC/Clang warning-clean; ASan/UBSan pass including 50,000 deterministic malformed inputs. Compiler/execution, native optimization, FFI and comparative benchmarks remain outstanding.

**2026-10-26T07:32:59Z**

Added runnable clean-room bytecode compiler and VM with unboxed ordinary locals, boxed captures, mutable closures, internal definitions, live global redefinition, fixed-arity calls, proper tail calls, numeric and pair/list primitives, and source-order evaluation with error recovery. Million-iteration loops and mutual tail recursion use at most two VM frames. Tests cover 8 KiB semispaces, every-allocation GC, exhaustion and recovery. GCC/Clang and ASan/UBSan pass; 17 programs plus four invalid programs agree with Chez. Saved baseline-vm.json from three startup-inclusive samples per workload/runtime: fib30 VM 0.167s vs Python 0.0525s; sum loop VM 0.0614s vs Python 0.0664s; pairs VM 0.00957s vs Python 0.0142s. Direct Scheme and Otium-on-Chez included, but steady-state performance remains unmeasured. Next: optimizing native compilation, FFI and shared-library coverage; successful code/constants also need lifetime management. Goal remains open.

**2026-10-26T08:09:17Z**

Added optional native tier (--native, --native=HEAT) that verifies bytecode control-flow stack heights, emits C, invokes GCC/Clang without a shell and loads native code. Includes guarded integer and pair operations, same-closure tail-loop optimization, exact stack roots at allocation boundaries, live redefinition and VM/native interoperability. Runtime suite passes eager and mid-execution tier transitions; GCC and Clang host/generated builds pass; ASan/UBSan instrument both host and generated modules, including every-allocation GC and large frames. Differential checks agree across VM/native/Chez for 17 programs and four errors. Added direct global invocation API and warm benchmark harness with correctness checks and a gate against compilation during timed samples. baseline-native.json records five warm samples: native is 2.82x/7.79x/6.53x faster than VM for fib/sum/pairs; sum and pairs beat Python 2.62x/2.25x but fib is 1.28x slower. Native remains 4.81x-19.42x slower than Chez. Temporary compiler artifacts are cleaned up. Native direct calls/value specialization, FFI, shared-library coverage, code lifetime management and broader performance/memory validation remain required.

**2026-10-26T09:04:37Z**

Implemented basic FFI using public library/test contracts: fixed scalar calls, UTF-8 copies and validation, typed null/foreign pointers, owned buffers, checked views, scalar memory access and release detection. Libffi development headers were unavailable, so typed C adapters use the shared module compiler and are cached by signature across libraries. Works in VM/native modes and through the direct C invocation API. GCC/Clang and ASan/UBSan checks pass, including instrumented fixtures/adapters, mixed register/stack arguments, every-allocation GC, 10,000 calls in an 8 KiB semispace and native-allocation cleanup on managed-heap exhaustion. Ten valid programs and six errors agree with Chez; existing 17-program/four-error differential suite still passes. Added runnable example-ffi.ot and warm baseline-foreign.json: one million integer calls take 13.0 ms native, 65.6 ms VM, 145.5 ms Python ctypes and 2.36 ms Chez. Callbacks, aggregate/variadic FFI, condition objects, namespace/shared-library coverage, metadata reclamation and broader performance/memory work remain. Goal stays active.

**2026-10-26T09:18:35Z**

Added mutable arrays to the clean-room prototype, including growable traced backing storage, callable lookup/defaults, mutation, shallow copy, structural membership and pair/array equality. Equality uses a host work list and visited-pair hash set, avoiding C recursion and terminating on cycles. Chez cyclic-array equality did not terminate in a bounded probe; cycle support is documented as a prototype extension.

Native code guards the saved callee before inlining array lookup, length and append with spare capacity; growth uses the shared rooted implementation. Tests cover redefinition during argument evaluation, aliases across growth/GC, shallow copies, popped-object reclamation in 8 KiB, growth exhaustion/recovery, 20,000-element list equality and cycles. VM/eager-native/tiered suites, foreign regressions, and VM/native ASan/UBSan pass. Differential checks now agree with Chez on 25 programs and eight error cases. LLVM development tools remain unavailable for optional GC lint.

Extended warm benchmarks with array build/indexed sum. Native median 0.368 ms versus VM 7.435 ms, Python 1.708 ms and direct Chez Scheme 0.367 ms. Native array fast paths reduced the first measured 1.095 ms kernel by about 3x. Updated baseline-native.json and DESIGN.md. Fibonacci remains slower than Python and much slower than Chez. Overall goal remains active; tables, namespace/macros/rest/conditions, code/metadata reclamation and broader optimization remain outstanding. No original C runtime or repository history consulted.

**2026-10-26T09:30:06Z**

Added managed insertion-ordered hash tables, callable tables/keywords, nil deletion, shallow copy, ordered keys/values, table membership and identity equality. Storage uses traced key/value slots plus opaque 32-bit open-address bucket indexes; tombstones compact without growth when enough entries have been deleted. Identity hashes survive copying GC in the existing 16-byte header (23 hash bits, eight kind bits, one frozen flag). Equal pair keys preserve identity semantics for nested arrays/tables.

Chez probes showed that pair keys freeze even on missing lookup or nil insertion. Implemented recursive pair freezing with an explicit work list and mutation errors. Pair hashing uses an eight-level structural prefix, with full cycle-aware equality for collisions. Growth stress caught and fixed a duplicate-root registration before validation.

Validation: VM/eager-native/tiered tests, heap identity preservation tests, foreign regression tests, and VM/native ASan/UBSan pass. Differential suite agrees with Chez on 32 programs and 13 errors. Added tests for table growth, compaction, order, frozen/cyclic keys, copying, identity keys across moving GC, 8 KiB churn and failed-growth recovery. Public array-equality checks pass with a minimal replacement for the test-module import. The public 100,000-entry tables workload completes in VM/native modes using an 8 MiB semispace. Optional LLVM tools remain unavailable.

Warm tables10000 median: native 0.458 ms, VM 1.832 ms, Python dict 0.512 ms, direct Chez eqv hashtable 0.301 ms. Direct native get/put! helper paths preserve callee guards/root publication but did not demonstrate a clear speedup from the first 0.455 ms measurement. Updated benchmark source, baseline-native.json and DESIGN.md. Overall performance target remains unmet, with recursive call performance, broader language coverage and memory reclamation still open. No original C runtime/history consulted.

**2026-10-26T09:35:57Z**

Reduced call-path overhead in the clean-room VM/native tier. Existing stack/frame capacity bypasses growth helpers, non-tail entry skips its no-op argument memmove, and return reuses the frame's closure slot. Generated non-tail self calls with unboxed parameters use guarded inline frame entry, preserving saved-callee semantics and the dispatch trampoline without C recursion. Capacity checks still enforce configured stack limits.

Added stress cases for managed values retained across recursion, boxed recursive parameters, and callee replacement during argument evaluation. VM/eager-native/tiered tests, VM/native ASan/UBSan, foreign regressions, and 32-program/13-error Chez differential checks pass. Existing tests cover frame/operand growth, stack-limit recovery and constant-space tail calls.

Alternating three runs each of saved before/after binaries (15 warm Fibonacci samples each) measured 57.319 ms before and 24.384 ms after, a 2.35x improvement. Refreshed baseline-native.json records Fibonacci native 25.299 ms, Python 43.242 ms, Chez 2.850 ms. This reverses the previous Python deficit but leaves roughly a 9x Chez gap. Ruby/Lua executables are unavailable and remain unmeasured. The broader goal remains active; no original C runtime or repository history consulted.

**2026-10-26T09:47:35Z**

Added code/constant reclamation at top-level evaluation boundaries. A joint walk of managed roots and code dependencies keeps live closures, nested-function metadata and constants, including old closures saved inside cyclic data. Unreachable functions release bytecode/capture metadata/native modules. Dead constant slots clear before semispace collection; function and constant slots are reused without renumbering live code. Weak symbol/keyword interning preserves identities reachable through user data. FFI's null constant stays pinned. Transactional compile flags make rollback safe when slots are reused.

New reload stress tests run 2,000 VM redefinitions and 40 eager-native redefinitions in an 8 KiB semispace with collection at every allocation. Saved versions, nested code, intern identity and recovery after failed compilation pass. Observed metadata stays bounded at seven function slots and nine constant slots; sampled boundary heap use peaks at 768 bytes VM / 760 native (not allocation-time peaks). Dropping old closures releases their metadata on the next evaluation.

Validation: reload VM/native ASan/UBSan, full runtime VM/native ASan/UBSan, VM/eager-native/tiered suites, foreign regressions, 32-program/13-error core differential and 10-program/6-error FFI differential checks pass. Refreshed warm benchmark results; Fibonacci native 23.684 ms versus Python 42.713 ms and Chez 2.855 ms. A noisy sum result was checked with three separate reruns, which returned 6.44–6.50 ms, consistent with the prior call-path baseline.

Reclamation currently walks/collects before every form, adding source-load overhead. Metadata arrays retain peak capacity; foreign records and unique global names still need lifetime work. Goal remains active. No original C runtime/history consulted; optional LLVM development tools remain unavailable.

**2026-10-26T09:54:35Z**

Added rest-argument functions in the clean-room compiler/runtime: dotted lists, ampersand final parameters, and single-symbol all-rest forms, including named definitions. Metadata records minimum arity/rest presence. Entry packs extra arguments into a fresh proper list before tail-frame reuse, rooting both incoming arguments and the partial list across moving GC. Captured rest parameters receive fresh boxes. Empty unboxed rest calls allocate nothing; fixed-arity native self-call paths remain unchanged.

Tests cover all syntax forms, rest-list freshness, captured parameters, recursive GC, constant-space tail calls, direct runtime_call, malformed/duplicate parameters, too few arguments, and packing-time heap exhaustion/recovery. Ten thousand empty-rest calls leave collection count unchanged with every-allocation GC enabled. VM/eager-native/tiered suites, ASan/UBSan including generated modules, reload tests and foreign regressions pass. Chez differential coverage now passes 38 programs and 20 error cases.

New warm rest100000 kernel: native 3.540 ms, VM 12.421 ms, Python 5.514 ms, Chez 0.306 ms. Fixed-arity Fibonacci remains 23.608 ms. Updated baseline-native.json and DESIGN.md. Optional parameters, apply, namespaces/macros and other language gaps remain, along with rest-allocation elimination and the broader performance target. Goal remains active; no original C runtime/history consulted.

**2026-10-26T10:03:00Z**

Added apply across VM, native and direct runtime_call dispatch. It supports leading arguments and a final proper list, array or nil (matching Chez probes). Expansion validates sequence shape/cycles and stack limits, then flattens values into the rooted VM stack without managed allocation. Nested apply is iterative; tail apply reuses the caller frame. Targets include closures, rest functions, builtins, callable collections/keywords and foreign bindings. Native dispatch guards the saved callee, preserving redefinition during argument evaluation.

Tests cover all sequence forms, nested apply, tail-space bounds, non-tail recursion, captured values under every-allocation GC, direct API use, malformed/cyclic inputs and stack-limit recovery. VM/eager-native/tiered suites, ASan/UBSan, reload tests and foreign regressions pass. Chez differential checks pass 46 core programs/25 errors and twelve FFI programs/six errors. The mutation comparison uses a local apply binding because Chez imports are immutable.

Added apply100000 warm benchmark: native 2.159 ms, VM 10.318 ms, Python 7.422 ms, Chez 0.398 ms. Updated baseline-native.json and DESIGN.md. This kernel beats Python by about 3.4x but still trails Chez by about 5.4x. Optional parameters, namespace/macros and broader runtime/compiler performance remain open. Goal remains active; no original C runtime/history consulted.

**2026-10-26T10:12:29Z**

Added optional parameters to the clean-room compiler/VM/native tier. Required and optional slots are contiguous; defaults compile into an entry prologue using an internal PRESENT instruction. Entry accepts the required-to-total arity range and initializes omitted slots without packing an argument list. Defaults execute left to right, see preceding parameters, and resolve their own names in the outer scope, matching Chez. Explicit nil/false remain supplied values. Captured optional slots are boxed before defaults run. Rest/optional mixtures are rejected.

Tests cover omitted/partial/full calls, default order and side effects, scope/shadowing, temporaries in defaults, captured closures during moving GC, fresh defaults across tail reuse, non-tail recursion, apply/direct API calls and invalid arities/declarations. Ten thousand integer-default calls trigger no collections with every-allocation GC enabled. VM/eager-native/tiered suites, ASan/UBSan, reload and foreign regression tests pass. Chez differential coverage now passes 53 programs and 32 errors.

New optional100000 benchmark: native 2.355 ms, VM 10.974 ms, Python 3.903 ms, direct Chez 0.220 ms. Updated baseline-native.json and DESIGN.md. Native is faster than Python here but remains roughly 11x behind Chez, so call specialization and broader performance work remain necessary. Namespaces/macros, conditions and remaining library/runtime coverage are still open. Goal remains active; no original C runtime/history consulted.

**2026-10-26T10:23:11Z**

Extended native integer arithmetic/comparisons to three through eight operands. A saved-callee guard and combined integer-tag mask select straight-line checked operations; larger arities and mixed types retain generic behavior. Every intermediate arithmetic result is checked, including overflow later canceled by another operand. Binary code generation remains unchanged: an experiment using the combined mask for binary calls regressed Fibonacci and was discarded.

Added tests for arities on either side of the specialization boundary, chained comparisons, mixed exact/inexact operands, invalid later operands after false comparisons, intermediate overflow, and callee replacement during argument evaluation. VM/eager-native/tiered suites and VM/native ASan/UBSan pass; Chez differential coverage now passes 57 programs and 32 errors. Chez's larger-integer semantics mean overflow checks remain prototype-specific.

Five alternating before/after runs (25 warm samples per kernel) measured optional 2.234 -> 1.808 ms, rest 2.570 -> 2.276 ms and apply 2.052 -> 1.909 ms: approximately 19%, 11% and 7% lower latency. Fibonacci stays about 22.2 ms. Added benchmark_compare.py and comparison-nary.json with samples/hashes, and refreshed baseline-native.json. Earlier separate-tag guard results were mixed; the retained version uses combined tags only for multi-operand operations. Overall performance and language-coverage goal remains active; no original C runtime/history consulted.

**2026-10-26T10:41:16Z**

Added foreign metadata reclamation after moving GC, including collections triggered inside long-running evaluations. Surviving managed wrappers mark bindings, signature adapters and allocation records. Dead bindings and unreferenced released records are freed; live aliases retain released records so use-after-free checks remain valid. The signature cache retains live/pending adapters plus at most four unused adapters after collection, evicting older modules. In-progress wrapper construction pins its adapter across GC.

Repeated open-library calls reuse an existing record when the OS returns the same loader handle, balancing the extra reference. One reference per distinct library remains until shutdown: raw returned pointers and native retention can outlive managed wrappers, so automatic library unload is not yet safe. Unreleased native buffers also retain their explicit ownership policy. Added RuntimeForeignStats diagnostics.

New tests perform 300 VM/30 native reopen/rebind cycles, signature eviction, 2,000 allocate/free operations within one evaluation, released-alias checks, live bindings held only in containers and a buffer retained only by C. Observed reopen/rebind counts stay at one library and two bindings, without repeated adapter compilation; unused adapters settle to four. VM/native foreign/reload ASan/UBSan passes, as do core/FFI regressions and both Chez differential suites (57 core programs/32 errors, twelve FFI programs/six errors). LLVM development tools remain unavailable.

Paired benchmark artifact shows pairs50000 0.964 -> 0.720 ms and ffi1000000 essentially unchanged at 12.87 ms; no slowdown observed in those checks. Refreshed native/FFI baselines and lifetime documentation. Distinct library lifetimes, unreleased native ownership, namespace/macros/conditions and broader compiler performance remain open. Goal stays active; no original C runtime/history consulted.

**2026-10-26T10:59:38Z**

Added bounded direct native self calls for fixed-arity closures with unboxed parameters. Each call retains its VM frame and exact roots; a shared dispatch driver stops at the suspended caller when a callee yields. Direct nesting is capped at 32 across mixed continuations, falling back to VM dispatch beyond that limit. Proper tail frame reuse remains intact. Generated direct calls target a private static body to avoid interposition between modules. Added native-depth diagnostics to the runtime API and benchmark JSON.

New tests execute 10,000 recursive calls with a 256 KiB OS stack, verify stack-limit error recovery and balanced nesting counters, and exercise moving GC and mixed VM/native continuations. Core VM/eager-native/tiered suites, reload and FFI suites, host/generated ASan/UBSan, and both Chez differential suites pass (57 core programs/32 errors; 12 FFI programs/6 errors). LLVM development tools remain unavailable.

Five alternating paired runs (25 samples per kernel) measure Fibonacci 22.412 -> 16.828 ms, about 25% lower latency; seven other kernels stay within roughly 4%. comparison-direct-calls.json preserves samples and executable hashes; the old executable uses frozen matching headers across the internal ABI change. Refreshed cross-runtime baseline: Fibonacci 16.823 ms native, 43.548 ms Python, 2.859 ms Chez. Still about 5.9x slower than Chez on this kernel. General inter-closure calls, broader language coverage and compiler improvements remain open. Goal remains active; no original C implementation or repository history consulted.

**2026-10-26T11:07:40Z**

Fixed lexical locals retaining managed objects after their scopes ended. The compiler now emits OP_CLEAR_LOCAL for non-tail let bindings, named-let initializer temporaries and while-body internal definitions. Nested scopes clear only their own bindings. VM/native cleanup discards the slot without mutating captured boxes; returned operands and escaping closures retain their values. Tail returns and frame reuse keep their existing cleanup.

A 32-scope workload building temporary 300-element lists previously exhausted semispaces through 256 KiB and first succeeded at 512 KiB among tested powers of two. It now succeeds at 16 KiB (32 KiB for both semispaces), in VM and native modes. comparison-scope-memory.json preserves source, results and executable hashes. This is managed heap capacity, not total process RSS.

Added test-scope-roots to default tests, covering VM/eager-native/tiered execution, GC at every allocation, mutable escaping captures, per-iteration internal definitions, managed return values and named-let closures. Focused host/generated ASan/UBSan passes, as do runtime/tiered/bounded-recursion suites, reload/foreign suites and both Chez differential suites (57 programs/32 errors; 12 FFI programs/6 errors). Optional LLVM development tools remain absent.

Extended paired benchmark harness to measure VM as well as native execution. Eight-kernel artifacts retain five alternating runs each. No consistent performance gain is claimed: native rest calls initially slowed 2.352 -> 2.588 ms but repeated at 2.281 -> 2.258 ms; pair-allocation timing also varied. Full liveness within scopes and stack-slot reuse remain open, along with broader compiler/language work. Goal stays active; no original C implementation or repository history consulted.

**2026-10-26T11:17:46Z**

Added lexical stack-slot reuse after capture analysis. Compiler-local lifetime records preserve logical binding identities until nested closures have been compiled. Two end-ordered priority queues reuse physical slots only across disjoint lexical lifetimes with the same boxing policy. Parameter positions remain fixed. The pass rewrites local bytecode operands and immediate-child capture indices, shrinks retained boxing metadata, and frees its temporary mapping/queues. Assignment is O(L log L) with O(L) temporary compiler storage and no runtime mapping lookup.

A 256-scope function now has one physical local and runs with a 32-slot stack in VM and native modes. The previous executable failed limits through 256 slots and first succeeded at 512 among tested sizes. comparison-slot-reuse.json retains source, hashes and results. New tests verify independent escaped mutable boxes, transitive captures, mixed boxing policies without extra boxing, nested shadowing, loop definitions and optional/rest parameters with allocation-by-allocation GC.

VM/eager-native/tiered runtime suites, bounded-recursion/scope-root tests, reload/foreign suites and both Chez differential suites pass (57 core programs/32 errors; 12 FFI programs/6 errors). Host/generated ASan/UBSan passes for slot reuse, scope roots and the runtime suite. LLVM development tools remain unavailable.

Five alternating native benchmark runs are in comparison-slot-native.json: Fibonacci 16.853 -> 16.637 ms; rest calls about 2.39 ms before/after. Existing kernels do not directly stress sequential scopes, so no general speedup is claimed. Full within-scope liveness, broader language coverage and compiler performance remain open. Goal stays active; no original C implementation or repository history consulted.

**2026-10-26T11:32:51Z**

Optimized immediate integer decoding on targets whose compile-time representation/shift checks permit it. A memcpy bit copy plus arithmetic shift avoids out-of-range unsigned-to-signed conversion; the portable path remains available and can be forced with OTIUM_PORTABLE_INTS. GCC 15.2 and Clang 21.1 emit a register move plus one shift on this x86-64 host, versus eight instructions for the old GCC decoder.

Native three-through-eight-operand integer arithmetic now computes checked tagged intermediates. Addition/subtraction adjust the second tag before checked signed arithmetic; multiplication uses an even tagged operand and one decoded operand, then restores the tag. Each intermediate overflow remains an error. Binary arithmetic/comparisons retain the original generated forms: applying tagged operations/ordering throughout regressed the sum loop 6.492 -> 8.794 ms, so that variant was rejected. Experiment artifacts are retained.

Added test-value and sanitize-value: both representation paths cover endpoints, small exhaustive integers, powers of two, two million randomized signed round-trips and a million arithmetic operand pairs checked against independent range/division predicates. GCC/Clang and ASan/UBSan pass. VM/eager-native/tiered suites, bounded calls, FFI tests and both Chez differential suites pass (57 core programs/32 errors; 12 FFI programs/6 errors); allocation/root/reload regressions also passed during this work. No original C runtime/history consulted.

Final five-run paired results: sum 6.476 -> 6.446 ms, Fibonacci 16.968 -> 16.572 ms, rest 2.362 -> 2.260 ms, apply 1.868 -> 1.815 ms. Table timing outlier did not repeat; pair allocation remained variable, including 0.951 -> 0.987 ms on repetition. No broad speedup is claimed. Refreshed native/FFI cross-runtime baselines: Fibonacci 16.156 ms native vs 42.573 ms Python and 2.812 ms Chez; FFI loop 12.748 ms native vs 134.146 ms ctypes and 2.383 ms Chez. The wider compiler/language/performance goal stays active.

**2026-10-26T12:12:30Z**

Added a guarded native path for side-effect-free integer loops. Eligible functions use a private fixed-size C array for locals/operands so the host compiler can keep loop state in registers. Entry checks integer parameters and original arithmetic globals. The whitelist excludes allocation, managed literals, shared mutation, arbitrary non-tail calls, boxed locals and rest/optional parameters. Limits are 4,096 instructions and 128 combined slots (at most 1 KiB private array on 64-bit hosts, plus compiler stack storage).

The original VM value frame remains unchanged while the private path executes. Failed operand/self-tail checks replay through the ordinary native body; the whitelist prevents duplicated external effects. Successful returns use rt_leave and overflow retains the existing error path. Self tail loops reuse the private array. Added scalar entry/fallback diagnostics. Straight-line helpers retain the ordinary path after an initial broad variant regressed pinned apply timing by about 10%.

Focused eager/tiered tests cover late type changes, self-tail parameter changes, late calls to side-effecting functions (effects happen once), builtin/global redefinition, saved closures, captured-box updates, errors and recovery. Twenty-four generated branching arithmetic loops match VM results and assert fast-path entry. GCC and Clang-generated modules pass; ASan/UBSan instruments host and generated modules. Core runtime, bounded-call, scope-root, slot-reuse, reload, FFI and both Chez differential suites pass (57 core programs/32 errors; 12 FFI programs/6 errors). Optional LLVM development tools remain unavailable.

Added optional CPU affinity to paired comparisons and affinity metadata to all benchmark reports after observing substantial within-run timing variation. Final five alternating runs pinned to CPU 0: sum1000000 6.441 -> 0.885 ms (7.3x); eight other kernels remain within about 2.3%. comparison-native-loops-pinned.json contains final samples/hashes; broader and unpinned experiments remain separately recorded. Refreshed pinned cross-runtime baselines: sum 0.900 ms native, 18.230 ms Python, 0.575 ms Chez; arrays 0.356 ms native vs 0.345 ms Chez. Fibonacci still takes 16.654 ms vs Chez's 2.869 ms. Broader language/performance work remains open. Goal stays active; no original C implementation or repository history consulted.

**2026-10-26T12:55:57Z**

Expanded performance evidence without changing runtime/compiler behavior. Added floating-point accumulation with strict boxed-float result validation and object-identity table workloads; matched Python/Chez and LuaJIT references. Optional public-Lua-API C runner uses the installed LuaJIT library without headers/CLI. Reports interpreter and JIT-enabled modes separately, checks all answers, and discards samples if trace compilation starts during measurement. Error paths and deliberately late JIT warmup test pass; runner ASan/UBSan smoke passed (system LuaJIT uninstrumented).

Saved 60 steady-state rows and 6 FFI rows, plus three targeted repeated runs pinned to CPU 0. All have five positive samples and correct answers. Across 15 targeted samples: float native 10.272 ms / Python 14.794 / LuaJIT interpreter 3.040 / LuaJIT enabled 0.504 / Chez 2.461; object keys native 0.303 / Chez 0.203; integer sum native 0.924 / Python 18.131 / Chez 0.571. Full-suite float native was 19.111 ms; preserved this variation rather than selecting the fastest run. baseline-expanded-repeats.json retains all targeted runs. FFI native 12.736 ms versus LuaJIT enabled 2.374 and Chez 2.415. Ruby and standalone Lua remain unmeasured.

Added --cpu and --workloads to benchmark_steady.py. DESIGN.md records methodology, limitations and reproduction commands. Preserved current benchmark executable and matching native headers in /tmp/otium-before-float-specialization for a paired next-step optimization. Floating-point arithmetic still boxes each result and cannot enter the private integer path; unboxed floating loops are the next concrete performance target. No original C implementation or repository history inspected. Broad language/performance scope remains active.

**2026-10-26T13:09:29Z**

Implemented an allocation-free numeric loop path alongside the existing integer path. Private temporaries retain loaded object identity and carry computed doubles without boxing; only a returned computed float allocates. Original VM roots remain unchanged until return, with no private pointer accesses after final allocation. Pure fallback replays preserve live callee replacement and avoid duplicated effects. Exact mixed integer/double comparison, sequential overflow, signed zero, and multiply/add rounding are preserved. Numeric bodies are separate non-inlined functions with individually named temporaries; array temporaries and merged bodies were too slow. Generated modules disable FP contraction; excess-precision hosts retain ordinary execution.

Five alternating CPU-0 runs (25 samples/workload) in comparison-native-numeric-pinned.json: float accumulation 10.294 -> 0.802 ms (12.8x); integer sum 0.929 -> 0.929 ms. Other ten measured workloads include differences up to 3.7% slower and 1.3% faster. The discarded merged-body variant and targeted separation check remain in separate artifacts. Fresh six-runtime float timings: native 0.793 ms, Python 13.821, LuaJIT interpreter 3.017, LuaJIT enabled 0.472, Chez 2.403. baseline-native-numeric.json retains the fresh cross-runtime comparison. This does not resolve recursive-call or broader language gaps.

New numeric tests compare 21x21 input combinations across eight operators at eager/tiered thresholds, plus n-ary operations, tail loops, mutable captures, identity, late type errors/recovery, live replacement, side-effecting fallback and integer overflow before a later float. All run with GC stress. A warmed-up 100-iteration loop forces exactly one collection, proving only the returned float allocates. GCC and Clang-generated code pass; host/generated ASan+UBSan pass with leak detection disabled because LeakSanitizer fails under the environment's tracing setup. Core VM/eager/tiered, bounded calls, scope roots, slot reuse, reload, FFI/reload, Chez differential 57 programs/32 errors and FFI differential 12 programs/6 errors pass. LLVM development tools absent. No original C implementation or repository history inspected. Goal remains active.

**2026-10-26T13:17:22Z**

Added top-level defonce and sequential top-level begin evaluation, based on public reload/expander/reflection tests and direct probes of the primary Chez implementation. Existing user bindings preserve nil, false, mutable state and saved callbacks; initializer compilation and execution are skipped after a successful definition. Failed initialization remains retryable. One metadata byte per global distinguishes user definitions from initial builtins, allowing a first defonce to shadow a builtin while retaining ordinary def bindings. Both VM and native definition stores update this metadata. Local/expression defonce and function shorthand are rejected. Optional docstrings are accepted with existing def parsing; reflection/doc storage remains outside the implemented subset.

Sequential top-level begin children now compile/evaluate in order, making prior definitions visible to later defonce forms and preserving earlier effects after later compilation errors. Function-body begins retain lexical-body compilation. Tests cover 1,000 skipped reloads with bounded code/constants and no additional native compilations, state identity across old/new callbacks, failed-init recovery and invalid contexts. VM/eager/tiered GC-stress tests, GCC/Clang-generated code and host/generated ASan+UBSan pass (detect_leaks=0 due the known environment limitation). Expanded Chez differential suite passes 65 programs/42 errors. Existing native scalar/numeric, bounded-call, runtime eager/tiered, reload, foreign reload, scope-root, slot-reuse and FFI differential tests pass. LLVM development tools absent. DESIGN.md records semantics and limits. Original C and repository history remain uninspected. Namespace/module support and remaining performance/language gaps remain open; goal stays active.

**2026-10-26T13:31:40Z**

Implemented namespace-bound global resolution. Top-level ns selects a persistent namespace; runtimes initialize otium.core, ffi and user namespaces. Globals retain stable indices and precise roots, with a separate uint32 namespace owner per binding. Definitions bind in the selected namespace. Reads resolve local names, qualified names in known namespaces, then core imports; unresolved names reserve local forward-reference slots. Literal local slash names take precedence, matching direct Chez probes. Host runtime_call accepts qualified names/current-namespace names without allocating failed lookups. FFI functions and the null pointer now live in ffi and remain callable inside/outside that namespace.

Corrected prior flat-environment behavior: a new local shadow of a builtin no longer redirects callers already compiled against its core binding. Existing user-binding references still see replacement, as do core references when the core binding itself changes. Native replacement tests now explicitly establish the local arithmetic binding before compiling those callers. Defonce state is independent by namespace. Native code continues to use fixed global slots and guarded builtin shortcuts, with no extra namespace lookup in hot calls.

GC-stress namespace tests pass in VM/eager/tiered modes, including 1,000 switch/reload cycles with fixed namespace/global counts, qualified mutation, forward refs, captures, old/new callbacks, imported/local/core replacement, FFI access and invalid contexts. GCC/Clang-generated code and host/generated ASan+UBSan pass (known environment requires detect_leaks=0). Expanded Chez differential suite: 74 programs and 50 errors; FFI differential: 12 programs and 6 errors. Runtime, defonce, native scalar/numeric, bounded-call, reload, FFI/reload, scope roots and slot reuse checks pass. LLVM tools absent; no original C/history inspected.

Fresh CPU-0 timings in baseline-namespaces.json: native float 0.791 ms vs Chez 2.442; integer sum 0.911 vs 0.600; Fibonacci 17.098 vs 2.841 (LuaJIT enabled 4.678). No broad performance completion claimed. DESIGN.md documents semantics, metadata cost and remaining work. File loading, require aliases/refers, module reload bookkeeping, namespace reflection and macros remain open; grouped ns require clauses explicitly reject rather than silently ignoring imports. Goal remains active.

**2026-10-26T14:06:40Z**

Added source-module loading through require, CLI -L paths and runtime_load_path. Explicit paths are searched in order before . and lib, with .ot before .scm within each directory; dotted names map to directories. Supports flat/grouped/quoted specifications, aliases, referred names, multiple modules, reload, empty require, and ns require clauses. Module loading uses cached unloaded/loading/loaded states, rejects cycles and mismatched declarations, inherits the requested namespace when no ns is present, and restores the caller's namespace/loading context. Failed loads/reloads remain retryable, including missing files; prior effects and deleted definitions remain. Runtime/module metadata contains no managed pointers. Active sources share a 16 MiB budget and recursive loads are capped at 128, with tested recovery.

Imports resolve to stable global slots at compile time. Retargeted aliases/refers affect subsequent compilation while old callers retain prior slots. Refer only accepts public owned bindings; top-level def- now tracks privacy. Explicit imports conflict with local definitions. Alias names may equal their own target but cannot collide with another namespace. Qualified lookup can follow an inherited referred binding. Direct Chez probes revealed a primary limitation: calling such a qualified inherited procedure directly as a call head fails there, while saving the value and calling it succeeds. Prototype supports both; this known difference is documented and the differential case uses the saved value. when/unless now lower directly to branches (needed by the public retry fixture), preserving nil results, truthiness, expression-body restrictions and tail calls.

Focused tests use public search/suffix/retry/mismatch/cycle fixtures plus private imports, alias retargeting, 1,000 cached requires with stable metadata/no new compilation, partial reload effects, old/live callbacks, deleted definitions, missing-file retry, deep dependency and oversized-source errors. VM/eager/tiered GC-stress tests and host/generated ASan+UBSan pass with detect_leaks=0 (known environment limitation). Clang-generated module tests also pass. New module differential suite passes 15 programs/18 errors; existing Chez suite 74/50 and FFI suite 12/6 pass. Runtime, defonce, namespaces, native scalar/numeric, bounded calls, reload, FFI/reload, scope roots and slot reuse regressions pass. LLVM development tools absent. No original C implementation/history inspected.

Implementation and limits are recorded in docs/runtime-rewrite.md. Builtin otium.core/ffi modules are treated as already available; this does not load the higher-level standard library. General macros, syntax quotation, conditions, reflection, additional library operations and recursive-call performance remain open. Goal remains active.

**2026-10-26T14:39:24Z**

Implemented plain quasiquotation: unquote, splicing, nested depth, dotted tails and constant-subtree folding. Static portions preserve sharing; active unquotes evaluate left to right. Final splices return an arbitrary tail directly (including nil, arrays and improper/cyclic lists), while middle splices validate a proper acyclic prefix and copy its spine after later expressions have run. Constructor calls use protected builtin constants, independent of user/core cons or append bindings. Native call analysis recognizes those constants and uses the existing cons fast path. Root-only unquotes/final splices preserve tail-call position.

Compiler planning holds all managed constants in a separate precise root array, updating its location after host reallocation and reloading children after managed allocation. Flat planning/emission use explicit work arrays; static data is allocated once rather than reconstructed per invocation. Logical plan payload is limited to 16 MiB plus capacity overhead. The private runtime splice helper roots source/tail/copied result and detects cycles before allocation. It is absent from public builtin names. No original C implementation or history inspected.

Focused GC-stress tests pass in VM/eager/tiered modes and with GCC/Clang-generated code; host/generated ASan+UBSan pass with detect_leaks=0 due the known environment limitation. Tests cover static sharing, nested templates, mutation/evaluation order, protected constructors, cycle/type errors, tail recursion, compile-time exhaustion and exhaustion while copying a splice, with successful recovery. An 8,000-element dynamic template passes with a 256 KiB C stack. New Chez differential suite: 38 programs/11 errors, plus a direct core-constructor-shadowing check. Existing core 74/50, module 15/18 and FFI 12/6 differential suites pass, along with native scalar/numeric, bounded calls, reload, modules, FFI, scope-root and slot-reuse checks. LLVM development tools absent.

DESIGN.md records representation, semantics, resource limits and verification. This provides data templates needed for macros; defmacro, syntax quotation/hygienic expansion, conditions/reflection, broader library support and remaining recursive-call performance gaps stay open. Goal remains active.

**2026-10-26T15:03:37Z**

New goal prioritizes core language/public test compliance, libffi, and temporary removal of generated C execution. Replaced FFI C adapters with cached libffi interfaces, typed argument storage and widened integer result handling. FFI/reload tests, 12 differential programs/6 errors and ASan/UBSan pass; VM FFI also passes with nonexistent PATH/compiler. Added public-harness compliance runner: initial 3 pass/46 fail, with library bootstrap and launcher gaps; this is an inventory, not a complete goal gate. Native tier still present and is next for removal. Original C implementation/history remain uninspected.

**2026-10-26T15:07:15Z**

Removed generated-C backend and compiler subprocess helper, native runtime/function metadata, CLI modes, metrics, and native-specific test targets. Bytecode compiler/VM retained; differential and benchmark drivers are VM-only. VM semantic/root/reload/module tests, all four differential suites (74/50, 15/18, 12/6, 38/11 programs/errors), and runtime/FFI/reload ASan+UBSan pass. Runtime/FFI tests pass without PATH/compiler; executable imports libffi and no process-launch APIs. Broad core compliance remains incomplete; expansion/bootstrap are next. Original C/history remain uninspected.

**2026-10-26T15:12:08Z**

Implemented initial defmacro closures and compiler expansion with lexical shadowing, namespace lookup, variadic arguments, reload semantics, rooted argument data, copied syntax arena, cycle detection and bounded expansion. Forced-GC macro tests and ASan/UBSan pass; 12 Chez programs/3 errors agree, including empty macro bodies. Runtime/module/quasiquote regression tests pass. Macro-generated local definition predeclaration, expansion APIs, gensym/hygiene/syntax quotation and full core bootstrap remain next; goal remains active. No original C/history inspected.

**2026-10-26T15:24:32Z**

Integrated the shared Otium expander into the CLI instead of duplicating body/hygiene logic in C. Added documented adapters, nested rooted VM invocation, cond, symbol identity preservation, gensym, basic type/string primitives, list length, and Otium macroexpand wrappers. Shared tests cover macro-produced locals, begin splicing, syntax quotation, reserved names, capture avoidance and recovery; forced-GC and sanitizer checks pass. Macro differential now 18 programs/3 errors; other differential suites pass. Public array equality passes 17 checks; inventory advances to 4 pass/45 fail. Full core library loading and its missing runtime dependencies remain next. No original C/history inspected.

**2026-10-26T15:37:49Z**

Added condition hierarchy registry, condition helpers, explicit error values/tables/data, type-pred wrapper, and VM try/catch/unmatched rethrow. C stack does not represent protected frames: 10,000 nested protected calls pass at 256 KiB stack. Forced-GC and ASan/UBSan checks pass; 17 condition programs/3 errors agree with Chez, existing 74/50 language differential and runtime/macro/module/quasiquote/FFI regressions pass. Corrected error? to any typed table and nested string printing. Core loading now reaches defgeneric; full generics/classes, signal handlers/restarts/cleanup and other core work remain. Public inventory still 4/45. No original C/history inspected.

**2026-10-26T15:57:31Z**

Full shared otium.core now bootstraps in its namespace, including otium.loop. Added built-in class descriptors and fixed-arity primary generic methods with specificity/ambiguity checks, replacement, preserved identity and detached introspection. Macro wrappers preserve expansion through aliases. Added datum read-string, Otium-derived primitive helpers and basic printing, plus E0301 for closure arity. Fixed unbound core placeholders leaking across namespaces and defonce local shadowing. Forced-GC/ASan+UBSan and 10/4 generic differential pass; existing differential suites pass. Optional public suite reports 52 checks; inventory 5 pass/44 fail. eval-only expected-error assertions remain weak until eval exists. Full classes/around/next, condition recovery, ports/reflection and remaining core coverage stay open. No original C/history inspected.

**2026-10-26T16:13:42Z**

Implemented nested eval with preserved VM roots and handlers, shared expansion, namespace-retaining evaluator values, and opaque array/table/procedure syntax. Deferred code reclamation to idle outer boundaries so failed compilation preserves macro side effects and expanded begin forms retain their constants. Shared expander diagnostic codes reach conditions. Forced-GC and ASan/UBSan eval tests pass; public modules 16 checks and syntax quote 18 checks now pass. Public inventory 7 pass/42 fail. Long-running eval code reclamation, full classes, dynamic parameters, conditions, ports, reflection and remaining core primitives remain open. No original C implementation/history inspected.

**2026-10-26T16:21:09Z**

Implemented documented numeric primitives, bounded bitwise operations and conversions. Changed integer arithmetic to specified payload-width wrapping with unsigned operations; conversion and left-shift overflow remain errors. Fixed integer? for integral floats, unary division representation and float printing/number->string. Public arithmetic passes 198 checks, bounded bitwise 7; inventory 9 pass/40 fail. Numeric differential 15 programs/9 errors, sanitizer boundary checks and 10,000 float round trips pass, as do runtime/eval/no-compiler FFI regressions. Full core compliance remains open; strings now block sequence progress. No original C/history inspected.

**2026-10-26T16:30:12Z**

Added UTF-8 substring clamping and lookup, string ordering/formatting/parsing, character conversions/classification, symbol/name coercion, and managed buffers with geometric storage growth and snapshot copies. Derived split/join/trim/replace/case/string construction in Otium, reusing shared search. Fixed string/list membership, keyword-call defaults, copy nil and variadic display/write. Public sequence, printing, JSON, loops, reader datums and OTcl now pass: inventory 15 pass/34 fail. Forced-GC and sanitizer buffer tests pass, including 500 appends with a retained snapshot; existing runtime/expander/eval/no-compiler FFI and language/quasiquote differential checks pass. Full runtime compliance remains open.

**2026-10-26T16:36:48Z**

Added type, EOF singleton/predicate, buffer?, active-namespace reflection and binding documentation. Ordinary redefinition replaces docs; defonce skips preserve them; failed initializers leave old docs intact. Added variadic put! for arrays/tables exposed by reader-values. Public reflection/bootstrap/library modules and 65-check character suite now pass: inventory 19 pass/30 fail. Reader-values advances to missing handler-bind. Forced-GC and sanitizer reflection tests plus runtime/module/reload/no-compiler FFI regressions pass. C macro type remains :macro per spec, distinct from Chez current :native. Full core/class/parameter/condition/port work remains active.

**2026-10-26T16:44:28Z**

Implemented fresh named dynamic params, top-level defparam docs/type/printing and sequential with-params. Added precise roots for active bindings and restoration on normal exit, try unwind, nested VM/eval failure and host-call failure. Expander public suite passes 74 checks. Forced-GC/ASan+UBSan tests pass, as do 4,000 nested scopes on 256 KiB C stack and runtime/condition/eval/no-compiler FFI regressions. Shared param semantics agree with Chez on 14 programs/9 errors. Zero-argument-only calls and :param follow the C spec rather than Chez setters/:function. Restart/quit/cleanup integration remains pending with those features.

**2026-10-26T16:55:55Z**

Implemented handler-bind and signal with all initializers before install, innermost-last traversal, visible-chain masking, declining handlers, and replacement errors. Explicit and VM errors are signaled before unwinding and not signaled again by unmatched try rethrows. Nested primitive callbacks preserve masks. Reader-values passes 64 checks; first 16 public condition checks pass, then remaining restarts/cleanup/exit work is still open. Inventory 21 pass/28 fail. Forced-GC/sanitizer and 14-program/5-error Chez differential pass; 4,000 handlers and catchable recursive-signal limit verified on 256 KiB C stack. Param/condition/eval/no-compiler FFI regressions pass.

**2026-10-26T17:04:33Z**

Implemented inline bytevectors, collection integration, Bytevector dispatch and strict UTF-8 conversion. Public bytevectors: 63 checks pass; inventory 22 pass / 27 fail. Forced-collection ASan/UBSan checks pass (leak checking disabled); Chez differential 10 programs / 9 errors pass. Handler, string, runtime and compiler-free libffi regression checks pass. Ports and remaining core compliance still open. LLVM tools unavailable for optional gc-lint.

**2026-10-26T17:07:44Z**

Added ordered --load preloads, --path alias, and incremental REPL input with syntax/error recovery and no replay of completed forms. CLI and reader ASan/UBSan checks pass (leak checks disabled). Public inventory 23 pass / 26 fail; reader REPL now passes. Strengthened cleanup-error fixture to reject unbound-operation errors masquerading as cleanup; Chez passes, prototype correctly fails. Common conditions now reach missing restart-case instead of CLI usage rejection.

**2026-10-26T17:13:29Z**

Added VM restart scopes, managed restart introspection and nonlocal invocation through handlers/nested eval. First 37 public condition checks pass; full conditions remain incomplete because cleanup/exit are missing. Forced-GC and ASan/UBSan pass (leak checking disabled); Chez differential 17 programs / 8 errors; 4,000 nested scopes transfer on 256 KiB C stack. Handler/eval/runtime/compiler-free libffi regressions pass. Inventory remains 23 pass / 26 fail. Next required work: unwind-protect/cleanup on all transfers.

**2026-10-26T17:20:27Z**

Added VM cleanup scopes for unwind-protect/defer and call-with-cleanup, plus uncatchable exit/quit and embedding exit-status API. Public common conditions: 64 checks; exit/quit/cleanup-error pass. Inventory 27 pass / 22 fail. Forced-GC and ASan/UBSan cleanup checks pass; 4,000 nested scopes unwind on 256 KiB stack. Chez agreement 14 programs / 8 errors; explicitly verified 2 differences where specs/io.md preserves pending resource transfers but Chez replaces them. Interrupts and ports remain open. Compiler-free libffi and restart/handler/eval/runtime regressions pass.

**2026-10-26T17:24:30Z**

Added host-owned interrupt flag API and CLI SIGINT delivery at VM/allocation/evaluation safe points. Interrupts bypass handlers/catches, run cleanup with resource outcome :quit, and return CLI status 130 unless replaced by cleanup. Public interrupt test and six additional process scenarios pass; host API recovery/detachment/pending-call checks pass. ASan/UBSan interrupt/CLI checks pass (leak checks disabled). Cleanup/restart/compiler-free libffi regressions pass. Blocking-port interrupt integration remains open.

**2026-10-26T17:26:39Z**

Implemented current-jiffy, jiffies-per-second and current-second using monotonic/realtime clocks. Shared otium.time helper works. All 136 common library checks pass; inventory 29 pass / 20 fail. Forced-GC and ASan/UBSan clock bracketing checks pass (leak checking disabled); Chez differential 3 programs / 3 errors pass. Ports, classes/records and reader token support remain open.

**2026-10-26T17:35:13Z**

Implemented managed memory ports, byte/UTF-8 reads and scans, output snapshots, current-port params and routed printing. 68 applicable public port checks pass with forced GC and ASan/UBSan (leak checking disabled), plus snapshot/growth/closed/decode/allocation-free read checks. Chez differential 10 programs / 7 errors passes with stateful reads explicitly sequenced. Standard stream raw output/lookahead/blocking SIGINT checks pass; interrupt and compiler-free libffi regressions pass. Full ports still missing read, file/custom ports and open-port tracking. Inventory remains 29 pass / 20 fail.

**2026-10-26T17:41:19Z**

Added streaming read for memory/standard ports using a boundary scanner and existing syntax reader. All 174 public port-datum checks pass; inventory 30 pass / 19 fail. Forced-GC/ASan/UBSan cover 52 negative fixtures, adjacent datums, UTF-8 errors and a 5,000-element list (leak checks disabled). Open-pipe tests verify no wait for EOF or consumption of the next byte. Port differential: 14 programs / 11 errors agree with Chez. Broad ports now reach missing make-input-port; file/custom ports remain open.

**2026-10-26T17:51:28Z**

Added buffered custom input/output ports, partial callback handling and open-port registry. Rooted port/payload slots survive callback GC and nested VM entry; failed flush retries preserve confirmed write offset. All 89 public port checks pass under forced GC/ASan/UBSan (leak checking disabled), including extra restart/reentry/count/registry/growth checks. Chez port differential 19 programs / 11 errors passes. Inventory 31 pass / 18 fail. File ports and shutdown flushing of registered outputs remain open.

**2026-10-26T17:55:32Z**

Added explicit idle-host runtime_flush_ports and CLI flushing at normal completion/exit/quit. Successful flushing preserves exit status; failures are surfaced and partial writes remain retryable. Rooted registry snapshots tolerate callback closes; newly opened ports require a later pass. Forced-GC and ASan/UBSan host/CLI tests pass (leak checks disabled). Port differential remains 19 programs / 11 errors. Public inventory 31 pass / 18 fail; file-based exit flush still awaits file ports and script arguments.

**2026-10-26T18:02:43Z**

Verified script metadata and CLI -- arguments. Added forced-GC ownership/failed-replacement tests and CLI argument/preload coverage; normal and ASan/UBSan runs pass (leak checking disabled). Public inventory remains 31 pass / 18 fail; file cases now reach missing filesystem/file-port operations, and format corpus reaches missing otium.reader. Goal remains incomplete.

**2026-10-26T18:05:34Z**

Added file ports with buffering, append/exclusive/truncate modes, binary aliases, owned-resource cleanup and structured file errors. Forced-GC and ASan/UBSan file tests pass, including /dev/full; existing port/shutdown regressions pass. Public inventory improves to 34 pass / 15 fail. libc backend is provisional: specified embedding I/O callbacks and managed buffering remain unfinished, as do filesystem helpers and language gaps.

**2026-10-26T18:08:06Z**

Added filesystem operations, sorted listings, metadata, symlink predicate and current-directory. Public file-port fixture (32 checks) plus symlink/error/NUL checks pass under forced GC and ASan/UBSan (leak checking disabled). Runtime/libffi pass without a compiler on PATH. Public inventory now 36 pass / 13 fail. Remaining failures are class/record and reader dependencies; embedding I/O still unfinished.

**2026-10-26T18:15:48Z**

Added user class declarations, construction, slot access/assignment, accessors, copying, stable identity and lazy layout migration with rollback/retry. Initial public class checks plus migration cases pass under forced GC and ASan/UBSan; 8 programs/8 errors agree with Chez. Public inventory now 40 pass / 9 fail; matching/resources/resource exit/blocking-input interrupt newly pass. Next/around methods, property lifecycle, records, reader tooling, class caches and embedding I/O remain unfinished.

**2026-10-26T18:19:59Z**

Implemented lexical next methods, around methods, immutable escaped method snapshots, qualifier-aware removal/replacement/introspection and generic reset. All 151 public class checks pass under forced GC and ASan/UBSan; extra snapshot and 2000-tail-call tests pass. Chez comparisons: 8 method programs/7 errors plus existing 10 generic programs/4 errors. Inventory now 41 pass / 8 fail: five stop at record support, three at reader module. Goal remains incomplete.

**2026-10-26T18:23:14Z**

Added inline managed records and descriptor primitives; shared Otium declaration registry handles namespaces/reloads. All 45 public record checks and retention/identity/immutability tests pass under forced GC and ASan/UBSan; 6 programs/8 errors agree with Chez. Class/method regressions pass. Inventory now 43 pass / 6 fail; remaining failures reach class-properties or reader module. Goal remains incomplete.

**2026-10-26T18:27:03Z**

Implemented property construction, state mounting and updates with migration hooks seeing supplied properties before execution; rollback preserves old properties/state on failure. Public UI 136 checks and extra lifecycle tests pass under forced GC and ASan/UBSan. Six programs/five errors agree with Chez, including stale description access behavior. Inventory 46 pass / 3 fail; reader tooling/formatting remain, plus class caches and embedding I/O requirements.

**2026-10-26T18:31:14Z**

Added otium.reader/tokens using datum-reader validation and raw spans with Unicode positions/CRLF gaps. All 120 public checks plus GC retention/2000-token tests pass normally and under ASan/UBSan; exact fixture output agrees with Chez. Default inventory 48 pass / 1 fail: formatter corpus exhausts fixed 1 MiB semispace. Unchanged corpus passes with --heap=16777216 (116 files, 0 failures, 382 collections, max 118 frames). Next: CLI memory policy; broader caches/embedding I/O remain unfinished.

**2026-10-26T18:36:08Z**

Added bounded heap growth: CLI starts 1 MiB/space, max 64 MiB; explicit --heap and embedding heaps remain fixed unless opted in. Growth preserves roots/identity and recovers after exhaustion. New growth+CLI tests and ASan/UBSan pass. Full prototype test target passes after fixing namespace leak test startup-count assumption. All 49 inventoried public groups pass by default; corpus 116 files/0 failures, final semispace 16 MiB. Compiler-free runtime/libffi tests pass. Overall goal still active pending broader FFI/embedding/performance coverage audit.

**2026-10-26T18:45:41Z**

Shared otium.ffi now loads over the scalar libffi backend. Added copy!, read-bytes, write-bytes! and exact UTF-8 reads. Public bulk suite: 23 checks pass, including forced collection and ASan/UBSan via test-foreign; extra overlap, detached-read and lifetime checks pass. Core inventory remains 49/49. Public common FFI now reaches missing %layout; aggregates, callbacks and detailed error metadata remain unfinished. gc-lint unavailable (no llvm-config).

**2026-10-26T18:47:25Z**

Added opaque traced aggregate layout snapshots, with detached descriptions. Layout calculations stay in shared Otium. First 93 public common FFI checks pass through aggregate memory; complete suite stops at first by-value struct binding. Forced-GC and ASan/UBSan layout/foreign regressions pass. Aggregate ABI lowering and callbacks remain.

**2026-10-26T18:52:15Z**

By-value structs and nested arrays now use libffi type trees, with layout verification and structural cache keys. Arguments are copied; owned result storage is prepared before native execution. Public common FFI prefix: 156 passing checks. Dedicated aggregate, cache/reclamation, forced-GC and ASan/UBSan regressions pass; core remains 49/49 and foreign reload passes. Full common suite now fails on missing call-error library metadata. Union ABI classification and callbacks remain unfinished.

**2026-10-26T18:55:33Z**

FFI bindings now retain selected library paths, symbols and copied type descriptions. Call failures report library/symbol, argument index and expected type; result/arity failures distinguish absent fields. Binding publication also fixes a possible stale list link across GC. 166 public common checks pass before union calls. Forced-GC, ASan/UBSan aggregate/error and bulk regressions plus foreign reload pass. Unions, callbacks, ownership and remaining error metadata audit are unfinished.

**2026-10-26T19:43:35Z**

Applied codebase-cleanup.md without resuming the runtime completion work. Baseline: full prototype suite passed; core inventory 49/49. Formatted 77 existing prototype C/header files with clang-format 23.1.3 and repository settings; final formatting is idempotent. Formatting-only patch: /tmp/otium-cleanup-format.patch.

Extracted host-only scalar/aggregate ABI preparation and signature-cache lookup into foreign_types.c with a private header. Preserved pending-signature pins, wrapper roots, post-allocation list publication and partial-failure cleanup. Public embedding declarations remain in runtime.h. Added builtins.def as the ordered source for public IDs and names; verified all 268 mappings against the baseline. Centralized runtime header dependencies, included the new source in normal/sanitizer builds, and removed retired native-test cleanup commands. Updated docs/runtime-rewrite.md with current architecture, limitations and historical context.

Validation after refactoring: full prototype tests and core inventory 49/49 pass; foreign and reload regressions pass; Chez differential 74 programs/50 errors and FFI differential 12 programs/6 errors pass; compiler-free tests pass. ASan/UBSan foreign, foreign-reload and runtime targets pass, including 23 bulk and 39 aggregate/error checks. Leak detection disabled, not leak validation. Optional LLVM development tools unavailable for GC lint. Formatting and git diff --check pass. Final inventory: /tmp/otium-cleanup-compliance.json. Structural patch: /tmp/otium-cleanup-refactor.patch.

Remaining structure: foreign.c still combines native-memory dispatch, layout snapshots and condition context; builtins.c dispatch ranges still depend on definition order. The full common FFI suite remains blocked at union ABI classification. Callbacks, broader ownership/error metadata, embedding and performance completion remain outside this cleanup. Epic stays open.

**2026-10-26T21:16:52Z**

Reverified the existing codebase-cleanup.md implementation in the current worktree. No additional runtime refactor was needed. Full prototype test target and core compliance inventory pass (49/49); foreign and foreign-reload regressions pass. Chez differential checks pass (74 programs/50 errors), as do FFI differential checks (12 programs/6 errors). ASan/UBSan foreign and foreign-reload pass with leak detection disabled. clang-format 23.1.3 produces identical output for all 79 prototype C/header files; git diff --check passes. Existing separate formatting and refactor patches remain in /tmp/otium-cleanup-format.patch and /tmp/otium-cleanup-refactor.patch. LLVM development tools remain unavailable. Runtime completion and the documented FFI limitations remain open.

**2026-10-26T21:20:51Z**

Implemented union call lowering for x86-64 Linux SysV using merged member classifications with byte positions retained for nested layouts. The unchanged common FFI suite passes all 222 checks, including register pressure, indirect returns, offset unions, retained bindings and zero-length member rejection. Added the full common suite to forced-GC foreign regressions; ASan/UBSan passes with leak detection disabled. Foreign reload tests pass. Expanded compliance inventory builds fixtures and runs every public FFI group: 52 pass, 4 fail, 0 skip. Failures reach missing callback and ownership APIs. Other ABI targets explicitly reject union calls. Callback/reentry, ownership, embedding I/O, integration and performance audit remain open. Optional LLVM tools unavailable.

**2026-10-26T21:31:10Z**

Implemented libffi closures, rooted callback procedures and deferred condition/exit propagation through nested native calls. Callback handlers run at the signal site; outer restarts become escape errors. Wrong-thread and idle-host invocation abort before VM entry. Freeing an active callback defers trampoline release until the outermost call returns. Added native function-pointer result/memory conversion and released metadata reclamation. Implemented ffi/own with exactly-once custom release, source/view invalidation and distinct CPointer dispatch for owners. Ordinary C pointer results now have unknown extent with no inferred owner; pointer address equality permits released pointers. All 56 public core/FFI groups pass. Full common 222, callbacks 51, ownership 10 and bulk 23 pass under forced GC and ASan/UBSan, plus cleanup, self-release, retained procedures, 100 replacement cycles and subprocess exit/thread/idle-host checks. Compiler-free and foreign reload checks pass; affected class/condition regressions and differential checks pass. Leak detection disabled and LLVM gc-lint tools unavailable. Embedding I/O, error-metadata audit, integration and performance remain unfinished.
