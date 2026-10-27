# Optimizing compiler plan

Goal: one compiler for Otium, written in Otium, that compiles everything ahead
of time in the Lisp tradition. Its first target is a register VM in the C
runtime. Native code comes later from the same compiler. It should be fast
enough to make Otium competitive with small optimizing Schemes, keep live
redefinition of user code working, and be checkable without a backend.

This replaces the prototype's native tier, which shelled out to the host C
compiler. That approach is gone: 66-113 ms to compile one small function,
50-200 MB for the compiler process, a toolchain on every target, and no way to
optimize across calls because the C compiler can't see the GC's rules. FFI moves
to libffi.

## Decisions

These are settled unless something below proves them wrong.

- **Ahead-of-time, compile everything.** No interpreter tier, no heat counters,
  no type feedback. Every top-level form and module is compiled when loaded,
  as Chez does. Optimization levels trade compile time for code quality.
- **Builtins are immutable.** `+`, `car`, `get` and the rest of the core
  primitives can't be redefined or `set!`. The compiler treats them as
  primitive operations. This is the SBCL model (CLHS 11.1.2.1.2, enforced
  there with package locks).
- **User functions stay redefinable.** Redefining a module-level function is
  visible to every caller. Inlining them is still allowed, because inlining is
  where much of the speed comes from. The compiler records what it inlined and
  recompiles dependents on redefinition (see Redefinition).
- **No `set!` on another module's globals.** A module's globals can only be
  assigned by code in that module. Replacing another module's function, as
  tests do for mocking, is a redefinition and goes through `redef!` (see
  Redefinition).
- **One-shot continuations only.** No multi-shot `call/cc`, matching
  `threading-plan.md`. Continuations in the IR are second-class labels and
  frames live on stacks.
- **CPS with second-class continuations** for the optimizer's main IR, in the
  style of Kennedy and Guile. The inliner runs earlier, on a tree.
- **Register VM** with per-safepoint slot maps, so frames can hold raw floats
  and integers and the GC only sees live values.
- **libffi** for foreign calls. Embedded targets can worry about it later.
- **No unsafe level.** Type checks are only ever removed when the compiler
  proves them redundant. There's no setting that trusts the programmer.
- **Single return values.** Otium doesn't get Scheme-style multiple values.
- **Records have fixed layouts.** Classes have layouts that change on reload,
  tracked by layout versions (see Classes, records and generic functions).
- **Callable collections stay.** Tables, arrays, keywords and instances can be
  called. It costs a slow path in the call instruction, not a slower fast path.
- **A simple compiler stays forever.** The system can always be bootstrapped
  without a working optimizing compiler (see Bootstrapping).
- **Chez stays a runtime, not a target.** The Chez implementation keeps
  compiling Otium its own way. The new compiler never targets Chez. It's
  developed on Chez while that's the runtime that can run real Otium, with no
  promise it keeps running there afterwards.
- **No cross-compiling.** Code is compiled on the machine that runs it.
- **The bytecode format is unstable.** Images are a cache, so the format can
  change whenever it's convenient.
- **No write barriers yet.** If a generational collector arrives, barriers get
  added then.

## Pipeline

```
source
  -> reader
  -> expander (lib/otium/expander.ot, shared)
  -> Core                 resolve names, mark assigned vars, fix letrec
  -> cp0                  inline, fold, simplify (on Core)
  -> CPS                  second-class continuations, flat node table
  -> CPS passes           contify, simplify, types/ranges, primop
                          specialization, CSE, DCE, unboxing
  -> closure conversion   flat closures, well-known functions
  -> slot allocation      liveness, slot maps
  -> register bytecode    (later: machine code from the same input)
```

Each arrow is a small pass with one job. Passes are named, can be switched off,
and are followed by the verifier in test builds.

## Core

Core is what the expander's output gets resolved into. It's the language the
semantics spec describes and the language the reference interpreter runs.

Forms:

- `(const v)`
- `(lref x)`, `(lset! x e)` for lexical variables
- `(gref g)`, `(gset! g e)`, `(gdef g e)` for global cells
- `(if e e e)`, `(seq e ...)`
- `(let ((x e) ...) e)` (sequential, as Otium's `let`), `(fix ((f fn) ...) e)`
  for mutually recursive functions
- `(fn arity body)` where arity is required count, optionals with default
  expressions, and an optional rest variable
- `(call e e ...)` for unknown calls, which may apply a closure, table,
  array, keyword or instance; `(primcall p e ...)` for builtins
- `(gcall g e ...)` for generic function calls, `(slot-ref e k)` and
  `(slot-set! e k e)` for class slots
- `(prompt tag body handler)`, `(abort tag e ...)` for one-shot escapes
- `(dyn-push kind e)`, `(dyn-pop kind)` for handler, parameter and cleanup
  stacks

`try`, `restart-case`, `defer`, `with-params` and early exits all lower to the
last two pairs. `handler-bind` doesn't unwind, so it's a handler-stack lookup
and an ordinary call.

Resolution happens on the way in:

- Every variable is lexical or global. Globals are cells identified by
  namespace and name, the same thing the runtime uses.
- Lexical variables get flags: assigned, captured, captured-and-assigned.
- `letrec`-style bindings go through "Fixing Letrec (Reloaded)" so most become
  `fix` and the rest get explicit initialization checks.
- Every node keeps the source position the expander attached through
  `expander-locate`.

Macros are fully expanded before Core. Macro redefinition keeps its existing
meaning: callers need reexpanding.

## Semantics to pin down first

The language docs have drifted, and the optimizer can only preserve semantics
that are written down. Before any pass is written, the Core spec needs answers
to these:

1. **Evaluation order.** Proposed: left to right everywhere, callee before
   arguments. The C prototype already depends on this.
2. **Integer overflow.** Chez promotes to bignums, C raises an error. This
   matters more to the optimizer than it looks. With error semantics, a
   successful `(+ a b)` on fixnums is a fixnum, so range and type inference
   can carry facts forward. With bignum semantics the result is "integer", and
   the next operation has to check again. The fast path costs the same either
   way (add, check overflow, branch to a slow path).
3. **Identity.** Is `eq?` meaningful on numbers, characters and strings? Can
   quoted constants be mutated? These decide whether constants can be shared
   or duplicated.
4. **Floats.** `-0.0` and NaN under `=`, `eqv?` and `equal?`. Constant folding
   needs exact answers.
5. **Redefinition.** See the next section. The rule for running activations
   needs to be stated in the spec, not left to the compiler.
6. **Errors.** Whether condition messages, the point an error is raised, and
   partial effects before it must match exactly. If they must, the compiler
   can't move a possible error past an effect.
7. **Thread switches.** What a green thread is guaranteed to see at a switch
   point. If threads can be preempted at loop back edges, every back edge is a
   point where globals and mutable data can change.

## Redefinition

Every global is a cell. Calls to globals load the cell and call what's in it,
as SBCL does through fdefns. That's the baseline and it costs one load per call.

The optimizer may go further and make **assumptions** about a global: inline
its function, call it directly, or use its arity and return type. Each
assumption is recorded as a dependency:

- A global cell carries a version number and a "watched" bit.
- Compiled code lists the (cell, version) pairs it relied on.
- Any store to a watched cell, whether `def`, reload or `set!`, bumps the
  version and invalidates dependents. Dependents are recompiled, eagerly by
  default.
- Loading a cached image checks the versions and recompiles anything stale.

Rules for what can be assumed:

- Builtins: always. They're immutable.
- Globals defined as functions with `def`: yes.
- Globals holding data, `defonce` state, and anything that has ever been the
  target of `set!`: never. A global with `set!` anywhere is marked assigned
  for good and its dependents are invalidated.
- Lexical bindings that are never assigned: always, with no dependency needed.

**Running activations.** When `f` is redefined while a frame that inlined `f` is
running, the frame finishes with the code it started with. New calls get the
new code. This is the Common Lisp rule for inlined functions, and it means
there's no deoptimization or frame rewriting anywhere in the system.

The case that bites is a frame that never returns, like Manse's event loop.
If the loop body inlined a handler, a redefined handler won't take effect until
the loop restarts. Options, not yet decided:

- Leave it as documented behavior, and provide a `notinline` declaration.
- Never inline a global call that sits inside a loop in a function that also
  contains the loop's back edge. The loop pays a call, and redefinition always
  takes effect on the next iteration.
- A development profile that only inlines builtins and lexical functions, with
  release builds inlining globals. Simple, but dev and release then perform
  differently.

My preference is the second. It keeps the semantics uniform and the cost is
small, because hot inner work usually lives in functions the loop calls rather
than in the loop itself.

**Assignment vs redefinition.** `set!` on a global is only allowed from the
module that owns it, as R6RS does for library exports. That keeps every
assignment to a global visible when its module is compiled, so the "never
assume assigned globals" rule can be applied before any code relies on the
value. Without it, code compiled later in another module could `set!` a global
that a running frame had already assumed constant.

Code typed at the REPL while in a module's namespace counts as that module.

Replacing another module's function is a code change, not runtime state, and
gets its own form:

- `(redef! other/f new-value)` stores into the cell as a redefinition:
  bumps the version, invalidates dependents, leaves the global unassigned.
- `(with-redefs ((other/f value) ...) body ...)` does the same for the
  extent of `body` and restores the old values on exit, including on errors.
  It's the mocking form for tests, as in Clojure.

Mutable state shared between modules lives in the owning module and is changed
through functions it exports.

The existing code has 48 cross-module `set!`s across 14 files. Nearly all are
test mocks in `tests/ui/` and `manse/tests/`, which become `with-redefs` or
`redef!`. The others are `lib/otium/ui/window.ot`, which assigns
`ui/open-windows` (`defonce` state owned by `otium.ui`) and needs an exported
updater, plus one each in `manse/bench.ot`, `tests/modules/checks.ot` and
`tests/library/modules.ot` to look at individually.

## cp0: inlining and simplification

The inliner follows Waddell and Dybvig's cp0, as Chez and Loko do. It works on
Core, before CPS, and does inlining, constant folding, copy propagation,
dead-binding removal and simple conditional simplification in one walk.

- **Budgets.** Every inlining attempt has a size limit (how big the result may
  be) and an effort limit (how much work to spend before giving up). Both are
  counters, so the pass always terminates and code growth is bounded. Loko's
  defaults are 16 and 50, which is a reasonable place to start.
- **Context.** cp0 tracks whether an expression's value is used, used only as
  a test, or ignored, which lets it drop dead work and simplify tests.
- **Candidates.** Lexical functions, builtins (as primops), and global
  functions under the rules above. `for`, `map`, `get` and small library
  helpers are the obvious wins.
- **Across modules.** A compiled module stores Core for its small exported
  functions next to its bytecode, as GHC does with interface files, so other
  modules can inline them without the source. Private `def-` functions with no
  outside references are inlined freely.
- **Recursion.** A function isn't inlined into itself. Loops are left for
  contification.

## CPS

After cp0, Core converts to CPS with second-class continuations:

- Every expression names the label its value goes to. Labels take parameters,
  which makes them SSA blocks with block arguments under another name.
- Calls take a return label. Labels can't escape, so nothing allocates a
  continuation.
- Functions are still first-class nodes until closure conversion.
- The representation is a flat table of numbered nodes, like Guile's "CPS
  soup", rather than a nested tree. Passes are loops over the table, which
  keeps memory predictable once the compiler runs on the VM.

Why CPS rather than SSA or GHC-style direct style with join points: the three
are close to equivalent for the passes that run here, and CPS with labels is
the form Guile has shown working end to end for a Scheme targeting a register
VM. Guards fit naturally too: a type check is a branch with a named failure
label. If readable dumps turn out to matter more, direct style with join points
("Compiling without Continuations", Maurer et al.) is the fallback, and most
passes would port over.

Assigned variables that aren't captured become ordinary SSA values during
conversion. With one-shot continuations a frame can't be resumed twice, so this
is never observable. Variables that are captured and assigned get boxes.

## CPS passes

In rough order:

- **Contification.** A local function that always returns to the same label
  becomes a label. This is how loops written as named `let`, `for` or self tail
  calls become jumps.
- **Simplification.** Fold constants, eliminate unused labels and
  parameters, merge single-predecessor labels, propagate copies.
- **Types and ranges.** A forward analysis assigning each value a type from a
  small lattice (fixnum with range, float, pair, empty list, nil, boolean,
  string, symbol, keyword, array, table, closure, record of type R, instance
  of class C, foreign pointer, unknown).
  Branches refine it: after `(pair? x)` succeeds, `x` is a pair.
- **Primop specialization.** Generic operations become specific ones with
  checks: `+` on two values becomes "if both fixnums, add with overflow check,
  else generic add". Where the analysis proves the types, the check disappears.
- **Effects.** Every primop has an effect set: allocates, reads mutable
  memory, writes mutable memory, may raise, may call unknown code, may switch
  threads. Unknown calls get all of them. CSE and code motion consult this and
  nothing else.
- **CSE and DCE.** Within the limits the effect sets allow.
- **Unboxing.** Floats and fixnums that only flow between arithmetic
  operations stay raw. A box is allocated only where a value escapes to
  something that expects a tagged value.
- **Bounds and loop work.** Hoisting checks out of loops, removing bounds
  checks with range facts. Later, once the basics are measured.

## Closure conversion

Flat closures: a closure holds copies of the free variables it uses (boxes for
the captured-and-assigned ones). From Keep, Hearn and Dybvig's "Optimizing
Closures in O(0) Time":

- Functions whose every use is a direct call ("well-known") need no closure
  value at all; their free variables become extra arguments where that's
  cheaper.
- Functions with no free variables become static constants.
- Mutually recursive closures share where possible.

Calls to known functions become direct calls to a function index. They skip the
closure check and, when the arity matches exactly, the arity check too.

## Classes, records and generic functions

Otium has `defrecord` (3 uses today) and classes with multiple dispatch
through `defgeneric` and `defmethod` (23 generics and 110 methods across `lib`,
`manse` and `examples`). The Chez implementation in
`chez/otium/generic-core.sls` already has a per-generic dispatch cache, an
epoch bumped by `defmethod`, and per-site slot caches keyed on layout. The
compiler and VM keep that design and make it cheaper.

**Records** have fixed layouts. Field access on a value known to be record type
R is a type check and a load at a fixed offset. Redefining a record type creates
a new type: existing instances keep the old one, and code that relied on the
old layout is invalidated like any other dependency.

**Classes** keep their identity across reloads, but their slots can change. Each
instance points at a layout, and each layout has a version.

- Slot reads `(obj :key)` and writes `(set! (obj :key) v)` with a constant key
  compile to a slot instruction with an inline cache: (layout → index). A hit
  is a layout compare and a load.
- When the compiler knows the exact class and its current layout, it uses the
  offset directly and records a dependency on the layout.

**Generic calls** compile to a dispatch instruction with a small per-site
inline cache, holding two to four entries of (argument classes, generic epoch)
→ effective method.

- A hit costs a class load and a compare per dispatched argument, an epoch
  compare, and a direct call. That's roughly the cost of a closure call.
- A miss falls back to the generic's own dispatch cache, then to full method
  selection, and refills the site.
- `defmethod`, `defclass` reloads and `reset-generic!` bump the epoch. Every
  site misses once and refills. Nothing is recompiled.
- When the compiler knows every dispatched argument's exact class, for example
  right after `make`, it can select the method at compile time and inline it,
  recording a dependency on the generic's epoch. Inside a method specialized on
  `Button` the argument may be a subclass, so this doesn't apply there.

Inline caches are runtime state, not type feedback. The compiler doesn't read
them. Using them to guide recompilation is possible later, and out of scope
here.

**Calls to collections.** `(x :key)` with unknown `x` could be a closure call,
a table lookup or a slot read. It compiles to one "call with constant keyword"
instruction: closure → call, instance → slot cache, table → lookup. That makes
it as fast as an explicit `get` or slot read.

## Slot allocation and slot maps

Frame slots are the VM's registers. Allocation is liveness-based: two values
share a slot when their lifetimes don't overlap. Arguments arrive in fixed
slots so calls don't shuffle.

At every point where the GC can run (calls, allocations, loop back edges that
are safepoints) the compiler records a **slot map**: which slots hold live
tagged values. The GC traces exactly those. This does two things:

- Dead values are never retained, so the prototype's explicit slot-clearing
  instructions aren't needed.
- Slots can hold raw doubles and integers. The map says they aren't pointers,
  so float loops don't allocate.

Maps are small (a bitmap per safepoint, shared when identical) and come from
liveness data the allocator computes anyway.

## The register VM

Sketch, to be refined with the C runtime work:

- **Instructions** are 64 bits: an 8-bit opcode and up to three operands
  (16-bit slot numbers or a wider immediate). Operands name frame slots
  directly, Lua and Guile style.
- **Frames.** A frame is a contiguous block of slots. A call puts the callee
  and arguments in consecutive slots at the top of the caller's frame, and the
  callee's frame starts there. Single return value.
- **Instruction groups:**
  - moves, constants, global cell load/store
  - closure creation and free variable access, box create/load/store
  - jumps, branch on false, fused compare-and-branch for fixnums
  - call, tail call, direct call to a known function, return
  - checked fixnum arithmetic and comparison, with the generic version as the
    slow path
  - raw float arithmetic, box and unbox
  - pair, array and table access with type checks
  - record field access at fixed offsets
  - slot access, generic dispatch and keyword calls, each with an inline
    cache stored beside the function's constants
  - prompt, abort, dynamic stack push and pop
  - safepoint at back edges, for GC, interrupts and thread preemption
- **Entry.** Each function has a general entry (arity check, optional
  defaults, rest list) and a direct entry for exact-arity known calls.
- **Dispatch.** Handlers written once and expanded either into a `switch` or
  into separate functions chained with guaranteed tail calls (`musttail`,
  `preserve_none` on Clang). Measured, not assumed: the tail-call version's
  advantage over computed goto was a few percent for CPython once a compiler
  bug in the baseline was fixed.

The prototype VM's per-instruction overheads (reloading frame and function each
instruction, a native-tier check on every instruction, a helper call per stack
height change) don't carry over.

## Native code, later

The same input that feeds bytecode emission (CPS after closure conversion and
slot allocation) can feed a native backend:

- Slots map to machine registers and stack locations; slot maps become stack
  maps.
- Native and VM frames share layout, so they can call each other freely.
- Compiled in process by the Otium compiler. No external toolchain.
- x86-64 and AArch64 first.

Loko is the reference for this part: cp0, a low-level optimizer, graph-colouring
register allocation, and hardware-assisted type checks, at a size one person
maintains.

## Optimization levels

- **-O0.** Core, CPS, closure conversion, slot allocation. Only lexical
  inlining (beta reduction). Fast to compile, fully debuggable. The REPL default
  if compile time demands it.
- **-O1** (default). cp0 with default budgets, contification, simplification,
  types, primop specialization, unboxing.
- **-O2.** Larger budgets, cross-module inlining of exported functions, loop
  work.

Compile time is measured per top-level form from the first prototype on.

## Debug information

- Every instruction maps to a source position, carried from the expander
  through every pass on a best-effort basis.
- An inline tree records which source function each instruction came from, so
  backtraces show inlined frames (SBCL does this).
- At -O0, variable-to-slot maps let a debugger show locals. At higher levels
  some locals are gone and the debugger says so.

## Bootstrapping and where the compiler runs

The system must always be buildable without a working optimizing compiler, and
without a checked-in binary.

- **Development on Chez.** During phases 2-5 the compiler is an Otium library
  loaded by the Chez implementation, because that's the runtime that runs real
  Otium today. It reads expander output and produces Core and CPS, tested
  through the reference interpreters. Once it moves to the C runtime, keeping
  it working on Chez is not a goal.
- **The simple compiler.** The C runtime keeps a simple, non-optimizing
  bytecode compiler (today's `compiler.c`, retargeted to the register VM)
  permanently. It must handle all of Core, including prompts, optional and
  rest arguments and dynamic state, and it must stay correct. It never needs
  to optimize.
- **The bootstrap chain.** The C runtime and simple compiler compile
  `expander.ot` and the optimizing compiler's source without optimization.
  That compiler then compiles itself, producing the optimized compiler image.
  `expander.ot` already uses only core forms, which keeps the first step small.
- **Normal use.** The runtime loads the optimized compiler image when it's
  available and falls back to the simple compiler when it isn't.

Compiled images are a cache. They record their dependency versions, so a
reload where nothing changed recompiles nothing, and a format change just means
recompiling.

## Correctness

Most of the risk in this project is miscompilation. The plan relies on several
independent checks rather than getting each pass right by inspection.

- **Reference interpreters.** A deliberately simple interpreter for Core and
  another for CPS, both in Otium. Core's interpreter is the executable form of
  the semantics spec. Every program can run four ways on Chez with no backend:
  Otium-on-Chez directly, Core, optimized Core, and CPS. Results and printed
  effects must match.
- **Verifier.** Runs after every pass in test builds. Checks scoping,
  single definition, definitions dominating uses, label arity, labels not
  escaping, and that effect annotations agree with the primop table.
- **Pass switches and fuel.** Every pass can be disabled by name. Every
  rewrite consumes one unit of a global fuel counter. A miscompile is found by
  binary search on fuel down to the single rewrite that caused it (GCC's debug
  counters and LLVM's `opt-bisect` work this way).
- **Generated programs.** A random Core program generator, using the
  interpreters as the oracle, with automatic shrinking of failures. Run a few
  thousand a night.
- **Simple compiler as oracle.** Once the back end exists, every test runs
  compiled by the simple compiler and by the optimizer, on the same VM. Same
  runtime, same representations, so any difference is an optimizer bug. This
  is the strongest check available and the main reason to keep the simple
  compiler correct.
- **Level comparison.** The same tests at -O0, -O1 and -O2 must agree.
- **Redefinition stress.** A test mode that redefines every watched global
  between calls and checks callers see the new code, plus the existing reload
  tests.
- **Slot map checking.** A VM debug mode that writes a poison value into every
  slot not listed in the current map at each safepoint. Reading a dead slot
  then traps instead of silently working. Combined with GC at every allocation.
- **Existing suites.** The public tests, the prototype's differential suites
  against Chez, and the R7RS benchmark ports, all through the compiler.

Formal verification is out of scope. Translation validation for one or two
passes (closure conversion, slot allocation) could be added if bugs keep
appearing there.

## Phases

Each phase ends with something runnable and tested.

1. **Spec.** Core semantics, with answers to the seven questions above.
2. **Core.** Resolver from expander output, Core interpreter, verifier.
   Exit: the public tests pass through the Core interpreter on Chez.
3. **cp0.** Inliner with budgets, dependency recording, fuel. Exit: tests pass
   at -O1 through the Core interpreter; generated programs agree across levels.
4. **CPS.** Conversion, CPS interpreter, contification, simplification. Exit:
   tests pass through the CPS interpreter.
5. **Types and primops.** Type and range analysis, specialization, effects,
   CSE, unboxing.
6. **Back end.** Closure conversion, slot allocation, bytecode emission, in
   step with the register VM and the retargeted simple compiler in the C
   runtime. Inline caches for slots, generics and keyword calls. Exit: tests
   and benchmarks run on the VM; the bootstrap chain builds the compiler from
   source; simple and optimized output agree on every test.
7. **Native.** x86-64 and AArch64, after the VM numbers are known.

Phases 2-5 run on Chez and need nothing from the C runtime, so they can
proceed while the VM is redesigned.

## Open decisions

- Integer overflow: error or bignum (spec item 2).
- Loops and redefinition: which of the three options.
- Preemption points for green threads, and what they guarantee (spec item 7).
- Whether -O0 or -O1 is the REPL default, once compile times are measured.
- Whether to have checked type declarations (SBCL-style `the` or parameter
  types). With no type feedback, they're the main way to tell the compiler
  more than it can prove. They'd always be checked, never trusted.
- Evaluation order, `eq?` on numbers and strings, mutable quoted constants,
  `-0.0`/NaN, and error-point precision (spec items 1, 3, 4 and 6).

## References

- Oscar Waddell, "Fast and Effective Procedure Integration" (thesis chapter;
  basis of cp0 in Chez and Loko).
- Waddell, Sarkar, Dybvig, "Fixing Letrec"; Ghuloum, Dybvig, "Fixing Letrec
  (Reloaded)".
- Andrew Kennedy, "Compiling with Continuations, Continued" (2007).
- Maurer, Downen, Ariola, Peyton Jones, "Compiling without Continuations"
  (2017).
- Andrew Appel, "SSA is Functional Programming" (1998).
- Keep, Hearn, Dybvig, "Optimizing Closures in O(0) Time" (2012).
- Andy Wingo's posts on Guile's CPS soup and register VM (wingolog.org).
- Loko Scheme manual, https://scheme.fail/manual/loko.html
- CMUCL design notes on ICR and VMR (the ancestors of SBCL's IR1 and IR2),
  https://cmucl.org/docs/internals/html/The-Implicit-Continuation-Representation.html
- CLHS 3.2.2.3 (same-file assumptions) and 11.1.2.1.2 (CL package
  constraints).
