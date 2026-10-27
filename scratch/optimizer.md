# Optimizer interfaces

The contracts between the parts of the optimizing compiler, for the first
version: Otium front end and middle end, with the register VM as the only
backend. The design and reasons are in `native-compiler-plan.md` and
`native-compiler-internals.md` (repository root of the main checkout, copied
into `docs/compiler-plan/` here). Where this document and those disagree, this
one wins for now; they describe the end state, this describes what's being
built.

Not in scope yet: the native backend, safepoint maps, new typed VM opcodes,
inlining user globals across forms (and so dependency tracking), inlining
`otium.core` functions written in Otium, reference interpreters and fuzzing.

## Ground rules

- Everything runs on the C runtime: `./otium --runtime=c file.ot`. Chez is not
  a target and nothing needs to work there.
- Read `AGENT-LANG.md` before writing Otium.
- Work happens in the worktree `/home/me/code/otium-optimizer`, branch
  `wip-optimizer`. Don't commit, stash, reset or check out; the coordinator
  commits. Several agents share the tree, so touch only the files you own.
- Don't run `meson compile` on `build/` unless you own the C side. Others use
  the `build/otium` binary as it is.
- Keep to the API below. If you need something another part doesn't provide,
  write it in your own file and note it in your report, rather than editing
  someone else's file.

## Files

All under `lib/otium/compiler/`, namespace `otium.compiler.NAME`.

| file | contents | owner |
| --- | --- | --- |
| `core.ot` | Core node constructors, accessors, Resolve | front end |
| `inline.ot` | Inline pass (from the branch's `cp0.ot`) | front end |
| `types.ot` | type facts: bits, ranges, join | primops |
| `prims.ot` | the primop table | primops |
| `column.ot` | bitsets and small array helpers | IR |
| `ir.ot` | block IR tables, builder, accessors, analyses | IR |
| `ir-text.ot` | block IR printer and reader | IR |
| `verify.ot` | block IR verifier | IR |
| `convert.ot` | Core to block IR | Convert |
| `propagate.ot` | Propagate | Propagate |
| `closures.ot` | closure conversion | Closures |
| `vm-ops.ot` | VM operation table | VM backend |
| `select.ot`, `allocate.ot`, `emit.ot` | VM backend | VM backend |
| `driver.ot` | unit record, pass list, `compile-form`, hook | coordinator |

The earlier source-to-source compiler (its driver, `cleanup.ot` and source
emitter) is retired.

Tests: one file per compiler file, `tests/compiler/` plus the same name, using
`(require otium.test :refer (check report))`, ending with `(report)`.

## The unit and the driver

A unit is one top-level form. It's a table:

| key | contents |
| --- | --- |
| `:ns` | namespace symbol the form is compiled in |
| `:form` | the expanded form (a datum; pairs carry source offsets) |
| `:core` | Core tree, after Resolve |
| `:ir` | block IR, after Convert |
| `:vm` | the VM blob, after Emit |
| `:options` | table: `:verify` (run the verifier after each IR pass), `:passes` (list of pass keywords to skip) |

`(driver/compile-form form ns)` runs Resolve, Inline, Convert, Propagate,
Closures, Select, Allocate and Emit, then `%vm-install`, and returns the
installed function id. Any pass may raise a condition made with
`(core/unsupported what)`; the driver then returns nil and the runtime compiles
the form with `compiler.c` instead. So every part may support a subset and
fall back for the rest.

## C interface

New builtins in the C runtime, all in `otium.core`:

- `(%resolve-global ns sym)`: the cell id (fixnum) that `sym` names when
  compiled in namespace `ns`, as `compiler.c` resolves it, creating an
  unbound cell where `compiler.c` would. `sym` may be qualified (`a.b/c`).
- `(%global-cell ns sym)`: the cell for `sym` defined in `ns`, created if
  missing (what `def` in `ns` assigns).
- `(%cell-info id)`: table with `:name` (symbol, qualified), `:ns` (symbol),
  `:defined` (boolean), `:macro` (boolean), `:builtin` (the builtin's name as a
  symbol if the cell is a C builtin's own cell in `otium.core`, else nil),
  `:inlined` (true for the builtins `compiler.c` turns into opcodes, which can't
  be redefined).
- `(%form-offset x)`: the source offset of a located pair, or nil.
- `(%vm-opcodes)`: table from opcode keyword to number. Keywords are the enum
  names without `OP_`, lowercased, `_` as `-`: `:move`, `:call-global`,
  `:br-lti`.
- `(%immediate-bits v)`: if `v` can be loaded with `IMM` (its `ot_value`
  fits a signed 32-bit immediate), that 32-bit value as a fixnum, else nil.
- `(%vm-install unit ns)`: installs a VM unit (below) compiled in namespace
  `ns` and returns the function id of its entry.
- `(%set-optimizer! f)`: when `f` is a procedure, `evaluate_raw` calls
  `(f form ns)` for each form it would give `oti_compile` (after the existing
  `ns`, `require`, `defmacro` and `defonce` handling). A fixnum result is a
  function id to execute in place of compiling; nil means compile it with
  `compiler.c`. While `f` runs, nested evaluation uses `compiler.c` only.
  `nil` turns it off.
- Startup: with `OTIUM_OPTIMIZE=1` in the environment, after the core library
  loads, the runtime requires `otium.compiler.driver` and calls
  `(otium.compiler.driver/enable!)`.

### VM units

The argument to `%vm-install` is an array of function descriptors. Index 0 is
the entry: arity 0, no captures, run once by the runtime.

| key | contents |
| --- | --- |
| `:name` | string or nil |
| `:arity`, `:optional` | counts |
| `:rest` | boolean |
| `:locals` | registers above register 0 |
| `:code` | array of fixnums, five per instruction: `op x a b c` |
| `:constants` | array of values; `CONST` and `BR_EQK` use indexes into it |
| `:statics` | array of `[constant-index function-index]`: the installer stores a new capture-free closure of that unit function at that constant index |
| `:captures` | array of `[index local?]`: register in the parent frame when `local?`, else capture index in the parent closure (the VM's `boxed` flag is always false) |
| `:points` | array of `[pc offset]`, source offsets in pc order, or empty |

Instructions use the VM's layout (`docs/vm-design.md`): `bc` operands are
written as `b` = low 16 bits, `c` = high 16 bits. The installer rewrites unit
references: `CONST`/`BR_EQK` constant indexes to runtime constants, `CLOSURE`
function indexes to runtime function ids. Global operands are cell ids and
pass through. Jump targets are instruction indexes within the function.

## Core

Core stays the branch's format (`lib/otium/compiler/core.ot`): nodes are
tables with an `:op` key, variables are tables shared by binding and
references. Changes:

- Every node may have `:pos`, a source offset from `%form-offset`.
- `{:op :global :cell id :name sym}` and `{:op :gset :cell id :name sym :value
  e}`; `:def` nodes have `:cell` too.
- `{:op :primcall :name sym :args (e ...)}`: `sym` is the bare `otium.core`
  name, for a call whose head resolves to a cell with `:builtin` set. A builtin
  used as a value is `{:op :prim :name sym :cell id}`.
- Forms Resolve doesn't support raise `(core/unsupported what)`. Required:
  constants and `quote`, `if`, `begin`, `let` (sequential), named `let`, `fn`
  with optionals and rest, body definitions, `set!`, `while`, `and`, `or`,
  `cond`, calls, `def`, `def-`, `quasiquote` (lowered to `cons`, `list`,
  `append` primcalls), `try` with `catch` clauses, `defer` and
  `unwind-protect`. Everything else may be unsupported.
- Variables have `:captured` set by Resolve.

## Type facts

`types.ot`. A fact is an immutable array `[bits lo hi const]`:

- `bits`: a fixnum bitmask over `t-fixnum t-float t-pair t-empty t-nil t-false
  t-true t-char t-string t-symbol t-keyword t-array t-table t-procedure
  t-record t-other`. `t-any` is all of them; `t-none` is 0 and means "no
  information yet" (bottom).
- `lo`, `hi`: fixnum bounds when the fixnum bit is set, inclusive; otherwise
  ignored. Unbounded is the fixnum range's own limits (`fixnum-min`,
  `fixnum-max` in `types.ot`).
- `const`: the constant value when the fact is a single known constant, else
  the unique marker `types/no-const`.

Functions: `(fact bits &optional lo hi)`, `(const-fact v)`, `(fact-bits f)`,
`(fact-lo f)`, `(fact-hi f)`, `(fact-const f)`, `(const? f)`, `any`, `none`,
`(join a b)`, `(meet a b)`, `(subfact? a b)`, `(widen old new)`,
`(only? f bits)` (every possible value has one of these types),
`(never? f bits)`.

## The primop table

`prims.ot`. `(prims/lookup sym)` returns the entry for an `otium.core` name or
nil. An entry is a table:

| key | contents |
| --- | --- |
| `:name` | symbol |
| `:arity` | `[required variadic?]` |
| `:effects` | list of `:alloc :read :write :raise :call :switch` |
| `:fold` | nil, or `(fn (args) value)` on constant args; raises if folding isn't valid for these args (the caller catches and doesn't fold) |
| `:transfer` | `(fn (facts) fact)` |
| `:refines` | for predicates: `[bits-if-true bits-if-false]` for the first argument |
| `:vm` | nil, or the VM opcode keyword when the VM has an instruction for exactly this call (`+` with two args is `:add`) |
| `:branch` | nil, or the VM branch opcode for this predicate in test position (`<` is `:br-lt`, `pair?` is `:br-pair`) |

Variants made by specialization (`car/unchecked`, `fx+`) are entries too,
named with a `%` prefix so they can't clash with user names: `%car`,
`%fx+`. Each says which real primop it implements under `:base` and has its
own `:effects`. For now a variant's `:vm` is its base's opcode; typed opcodes
come later.

## Block IR

`ir.ot` owns the representation; everyone else uses these functions. Ids are
fixnums: functions `f`, blocks `b`, operations `o`, values `v`.

### Building

- `(ir/new)` → `ir`
- `(ir/add-function! ir name parent pos)` → `f`. `parent` is a function id or
  nil (the unit's entry has none).
- `(ir/set-signature! ir f params required optional rest?)`: `params` is a
  list of value ids made with `ir/add-param!` on the entry block, in order:
  required, then optional, then the rest parameter.
- `(ir/set-entry! ir f b)`
- `(ir/add-block! ir f)` → `b`
- `(ir/add-param! ir b)` → `v`
- `(ir/emit! ir b code args &optional aux pos)` → `v`, appending an
  operation to `b`. Every operation defines exactly one value.
- Terminators, exactly one per block, added last:
  - `(ir/jump! ir b target args)`
  - `(ir/branch! ir b test then else)`: branch on truth of `test`
  - `(ir/branch-prim! ir b prim args then else)`: `prim` is a predicate or
    comparison primop symbol; no boolean is materialized
  - `(ir/call! ir b kind callee args ret pos)`: `kind` is `:value` (callee
    is a value), `:global` (callee is a cell id) or `:known` (callee is a
    function id; Closures and the backend know its closure). The result is
    the first parameter of `ret`, which must have one or more parameters.
  - `(ir/tail-call! ir b kind callee args pos)`
  - `(ir/return! ir b v)`
  - `(ir/try! ir b body handler)`: install a handler and continue at `body`;
    a raise until the matching `pop-try` goes to `handler`, which has one
    parameter, the condition
  - `(ir/unreachable! ir b)`

### Reading

- `(ir/functions ir)` → list of `f` in creation order;
  `(ir/fn-name ir f)`, `(ir/fn-parent ir f)`, `(ir/fn-pos ir f)`,
  `(ir/fn-entry ir f)`, `(ir/fn-params ir f)`,
  `(ir/fn-signature ir f)` → `[required optional rest?]`,
  `(ir/fn-blocks ir f)` → list of `b`.
- `(ir/block-fn ir b)`, `(ir/block-params ir b)`, `(ir/block-ops ir b)` →
  list of `o`.
- `(ir/term-kind ir b)` → `:jump :branch :branch-prim :call :tail-call
  :return :try :unreachable`; `(ir/term-args ir b)` → operand values (test,
  prim args, call args, returned value); `(ir/term-callee ir b)`,
  `(ir/term-call-kind ir b)`, `(ir/term-prim ir b)`, `(ir/term-pos ir b)`;
  `(ir/succs ir b)` → successor blocks in order (jump: target; branch: then,
  else; call: ret; try: body, handler); `(ir/edge-args ir b i)` → values passed
  to successor `i` (only `jump` passes any; a call's return value is implicit).
- `(ir/op-code ir o)`, `(ir/op-args ir o)`, `(ir/op-aux ir o)`,
  `(ir/op-pos ir o)`, `(ir/op-block ir o)`, `(ir/op-value ir o)`.
- `(ir/value-def ir v)` → `[:op o]` or `[:param b i]`; `(ir/value-fn ir v)`.
- Counts and iteration: `(ir/value-count ir)`, `(ir/block-count ir)`.

### Facts on values and blocks

`(ir/value-fact ir v)` and `(ir/set-value-fact! ir v fact)`, initially nil.
Propagate fills them and later passes may read them. `(ir/value-name ir v)` and
`(ir/set-value-name! ir v sym)` for printing.

### Analyses

Computed on demand, not kept up to date by edits:

- `(ir/preds ir b)`
- `(ir/rpo ir f)` → reachable blocks in reverse postorder
- `(ir/dominators ir f)` → table `b` → immediate dominator (entry maps to
  itself), Cooper–Harvey–Kennedy
- `(ir/dominated-children ir doms b)`
- `(ir/uses ir)` → array indexed by `v`, each a list of `[:op o]` or
  `[:term b]`
- `(ir/loop-depths ir f)` → table `b` → depth

### Copying

Passes that rewrite write a fresh IR. `(ir/copy-function-shell! from to f)`
copies a function's name, parent and position into `to` and returns the new
id; passes copy blocks and values themselves, keeping their own value maps.

### Operation codes

An operation's `code` is a symbol: a primop name from `prims.ot`, or one of:

| code | args | aux | meaning |
| --- | --- | --- | --- |
| `const` | | value | the constant |
| `global-ref` | | cell id | read a global; raises if unbound |
| `global-set` | value | cell id | `set!` a global; raises if unbound; result is nil |
| `global-def` | value | cell id | `def`; result is nil |
| `prim-ref` | | symbol | a builtin as a value |
| `box` | value | | a new box holding it |
| `box-ref` | box | | contents; raises if unbound (an uninitialized letrec variable) |
| `box-set` | box value | | store; result is nil |
| `unbound` | | | the unbound marker, to initialize a box |
| `check-bound` | value | | raises if value is the unbound marker; result is the value |
| `bound?` | value | | #t unless value is the unbound marker (optional parameters) |
| `refine` | value | type bits | the same value, known to have these types |
| `closure` | captured values | function id | a closure (before Closures) |
| `self` | | | the running function's own closure |
| `env-ref` | | index | free variable `index` of the running closure (after Closures) |
| `make-closure` | captured values | function id | a closure with these captures in order (after Closures) |
| `static-closure` | | function id | the closure of a function with no captures (after Closures) |
| `pop-try` | | | leave the innermost `try`; result nil |
| `raise` | condition | | re-raise a caught condition as it is (the VM's `RAISE`); must be followed by `unreachable` |
| `push-cleanup` | closure | | install a cleanup; result nil |
| `run-cleanup` | | | run and remove the innermost cleanup; result nil |

Before Closures, a function refers to a value of its parent (or further out)
directly, as a free variable. After Closures, no function uses another's
values.

### Text format

`ir-text.ot`: `(ir-text/print ir)` returns a datum, `(ir-text/read datum)`
returns an IR, and reading a printed IR prints the same datum. Values print as
`v3`, blocks as `b2`, functions as `f1`; ids may be renumbered by reading.

```
(ir
  (fn f0 "fib" :parent nil :params (v0) :signature (1 0 #f) :entry b0
    (b0 ()
      (v1 const 2)
      (branch-prim < (v0 v1) b1 b2))
    (b1 () (return v0))
    (b2 ()
      (v2 const 1)
      (v3 - v0 v2)
      (call global 17 (v3) b3))
    (b3 (v4)
      (v5 const 2)
      (v6 - v0 v5)
      (call global 17 (v6) b4))
    (b4 (v7)
      (v8 + v4 v7)
      (return v8))))
```

Operations print as `(vN code arg ... [:aux x] [:pos n])` except `const`,
whose single datum after the code is the value (printed with `write`, quoted
where needed). Terminators: `(jump b args...)`, `(branch v b b)`,
`(branch-prim p (args) b b)`, `(call kind callee (args) b [:pos n])`,
`(tail-call kind callee (args) [:pos n])`, `(return v)`, `(try b b)`,
`(unreachable)`. `:fact` annotations may follow a value's definition when
facts are set: `(v3 - v0 v2 :fact (fixnum 0 10))`.

### Verifier rules

`(verify/check ir stage)` raises with a readable message on the first
violation. `stage` is `:converted` or `:closed`. Rules:

- Every block has exactly one terminator; every successor exists and belongs
  to the same function; `jump` passes as many values as the target has
  parameters; a call's `ret` and a `try`'s handler have at least one
  parameter.
- Every value is defined once and its definition dominates every use, where
  uses from a nested function (before Closures) count as uses at the
  `closure` operation that creates it.
- `:closed`: no function uses a value defined in another function; `closure`
  doesn't appear; `env-ref` indexes are below the function's capture count.
- Primop codes exist in `prims.ot`; argument counts fit their arity.
- Every function's entry has the parameters its signature says.

## Convert

Core → block IR, one function per `fn` (plus the unit's entry function), with
the jobs from the internals doc:

- **Contification** of `fix`-bound functions (named `let`, body definitions)
  that are only called, with all calls from outside the group returning to
  the same place. These become blocks; calls become `jump`.
- **Assignment conversion**: `set!` variables not captured are SSA values with
  block parameters at joins. Captured-and-assigned variables use `box`,
  `box-ref`, `box-set`. So do variables assigned in a `try` body and used
  after it.
- **Refinement**: in the branches of `(if (pred x) ...)` where `x` is a
  lexical variable and `pred` has `:refines`, `x` is rebound to a `refine`
  value.
- **Escaping `fix` groups**: a reference from a function to itself is `self`.
  References between members go through a box per referenced member, created
  with `unbound` before the closures and set after.
- **Optionals**: each optional parameter `p` with default `d`: branch-prim
  `bound?` on `p`; the false side computes `d`; both join with the value as a
  block parameter.
- **`try`**: `(try body handler)` terminator; the handler block's parameter is
  the condition; catch clauses test it with their predicates in order and
  re-raise (call `raise`) when none match. `pop-try` on every normal exit.
- **`defer`**: the cleanup becomes a closure, `push-cleanup` before the body,
  `run-cleanup` after it.
- **Top level**: the unit's entry function has no parameters; `def` is
  `global-def`; its value is returned.

## Propagate

Per the plan: optimistic constant, type and range propagation over each
function, then a rewrite in dominator order: fold constants, fold known
branches, drop unreachable blocks, specialize primops to their `%` variants
where facts allow, drop proven `check-bound` and `refine`, CSE of operations
with no `:raise`, `:write`, `:read`, `:alloc`, `:call` or `:switch` effects,
and removal of unused operations with no effects. Writes facts on the values
of the new IR. Calls of kind `:value` whose callee is known to be a
`closure` or `make-closure` of function `f` become `:known`.

## Closures

For the VM:

- Free variables of each function, in a fixed order, become its captures.
- In the body, a free variable becomes `env-ref i`.
- `closure f vs` becomes `make-closure f captures`, or `static-closure f`
  when it has no captures.
- A function whose every reference is as a `:known` callee and that has
  captures may instead take them as extra leading parameters
  (`set-signature!` updated), with callers passing them, when it has four or
  fewer. Its closure is then a `static-closure`.

## VM backend

`vm-ops.ot` defines the operations Select chooses from, one per VM instruction
form, with operand kinds (register, immediate range, constant index, cell id,
jump target). `select.ot` maps closed block IR to those operations on SSA
values; `allocate.ot` assigns registers; `emit.ot` writes the VM unit for
`%vm-install`.

VM conventions that constrain allocation (see `docs/vm-design.md`):

- Register 0 is the running closure. Parameters arrive in 1..arity, then the
  optional slots (unbound when not passed) or the rest list.
- A call puts the callee in register R and the arguments in R+1..R+n; the
  result comes back in R. Every register at or above R is clobbered.
- `CALL_GLOBAL` reads the global after the arguments are in place.
- `CLOSURE` copies its captures from fixed registers named in the function's
  `:captures`, so every captured local must be in that register at that
  instruction.
- `TRY` restores the frame and writes the condition to its register;
  registers live into a handler block must not be reused anywhere in the
  `try` body. The simplest correct rule: any value live into a handler keeps
  its register for the whole function.
- Back-edge jumps check interrupts (already the VM's behavior).

Allocation is liveness-based: values share registers when their lifetimes
don't overlap, call windows sit above everything live across the call, and
arguments whose last use is the call are allocated in their window slots.
Block arguments become parallel moves.

## Notes from the first wave

Core as built: the node list is at the top of `lib/otium/compiler/core.ot`.
Differences from the Core section above:

- `:fix :bindings ((x . fn) ...) :body`: every variable unassigned, every
  value a `:fn`. Fixing Letrec puts other letrec values in `:let`s, or binds
  them to `{:op :unbound}` and assigns them in order; a variable with
  `:unbound #t` starts out unbound, so reads of it need `check-bound` (or
  `box-ref` when boxed).
- `and`/`or` arrive as `let` plus `if`. `quasiquote` arrives as `cons`
  primcalls and calls to the global `otium.core/append` (an Otium function on
  this runtime).
- `:defer :body :cleanup` covers `unwind-protect` too.
- `:try :body :catches ((pred x body) ...)`: `pred` is nil for a catch-all.
- `core/annotate!` recomputes exact `:refs`, `:sets` and `:captured`.

The VM installer (`c/src/optimizer.c`, documented under "Installed code" in
`docs/vm-design.md`) enforces:

- Opcode keywords come straight from the enum: `:nullp`, `:pairp`,
  `:br-null`, `:call-global`.
- Every fused branch (`BR_*`) is followed by a `JUMP` word holding its target,
  and that word counts in instruction indexes. A branch jumps when its test
  equals `x`. `BR_*I` forms test register `a` against the immediate in `c`.
- The last word of a function is `JUMP`, `RETURN`, `RETURN_IMM`, `TAIL_CALL`,
  `TAIL_CALL_GLOBAL` or `RAISE`.
- Every register named, including whole call windows (`CALL a b` uses
  `a..a+b`, `CALL_GLOBAL x a` uses `a..a+x`), is at most `:locals`. Only tail
  calls may name register 0; nothing writes it.
- `IMM` takes `%immediate-bits`, written as `b` = low 16 bits, `c` = high 16
  bits. The unbound marker's bits are 18.
- `a`, `b`, `c` accept -32768..65535; `x` is 0..255, so `CALL_GLOBAL` takes at
  most 255 arguments.
- Not checked: what registers hold at run time. `UNBOX` and friends on a
  non-box crash.
- `%global-cell` creates a cell in that namespace; only call it for real
  `def`s, never to look a name up.

## Rules settled for the second wave

- **Global call order.** `(call :global cell args ret)` reads the cell when
  the call is made, after the arguments are computed (the VM's
  `CALL_GLOBAL`). Otium evaluates the callee first, so Convert uses `:global`
  only when no argument can call unknown code or assign a global (constants,
  variables, and primops without `:call` or `:write` effects on globals).
  Otherwise it emits `global-ref` first and makes a `:value` call.
- **Known calls after Closures** only target functions with no captures:
  static functions, and well-known functions whose captures became extra
  parameters. Any other call Propagate marked `:known` goes back to a
  `:value` call on the closure value. The backend loads a known callee with
  the function's static closure.
- **Primops without a VM opcode** are calls to the builtin's cell
  (`(%resolve-global 'otium.core name)`) in the middle of a block. The
  backend treats them as calls: they need a call window and clobber it.
- **Re-raising** is `(error condition)` on this runtime; there is no `raise`
  builtin. In the IR it's the `raise` operation.
- **Overflow** of fixnum `+ - *` wraps on this runtime (it doesn't raise).
  `types.ot` and `prims.ot` already account for it.
- **Commits** go through a hook that runs `./otium fmt --check` and the
  linter (which runs on Chez) on staged Otium files. Run `./otium fmt FILE`
  and `./otium lint FILE` on your files before finishing. Don't use C-only
  builtins as values at load time; for names the Chez linter doesn't know,
  add a `;; lint: allow-file(unbound-name) reason` line at the top, as
  `core.ot` does.
- **Symbols are slow to intern** on this runtime once there are many of them.
  Don't make a symbol per value or block; use fixnums and strings.
- `ns` and `range` can't be local variable names.

## Status

Every pass exists and runs on the C runtime. With `OTIUM_OPTIMIZE=1` the C
test suite (102 tests) passes, also with `OTIUM_OPTIMIZE_VERIFY=1`, and all
43 R7RS ports in this tree give the same results as compiler.c. Forms fall
back to compiler.c only for the unsupported forms listed under Core (class
and generic declarations, handler-bind, restart-case, with-params, defparam).

Generated code runs within 5% of compiler.c's in VM instructions on the
ports measured, either way. Known gaps, roughly in order of payoff:

- Call windows. compiler.c reserves a call's window before computing its
  arguments, so nested calls stack above it and results land in place.
  Allocate places each window above what's live across it, so results of
  nested calls often need moving (deriv, triangl, the float ports).
- No inlining of globals, including otium.core functions written in Otium
  (map, append, second, ...), which needs the dependency tracking from the
  plan. Most benchmark time is in calls between globals.
- No typed VM opcodes, so Propagate's specializations (%fx+, %car) still
  run the generic instruction's checks.
- Compile time is about 2.4 ms per top-level form, with the compiler itself
  compiled by compiler.c, and loading it adds about 0.1 s at startup.

Tools: `OTIUM_DUMP_CODE=1` prints compiler.c's code and every installed
unit; `OTIUM_OPTIMIZE_REPORT=1` prints each fallback. The dump only shows each
unit's entry function, not the functions nested in it.

## Next

The plan changed after this build; see "Revisions after the first build" in
`docs/compiler-plan/native-compiler-plan.md`. In short: the VM will take
Inline's output back to compiler.c, user functions are inlined only when
declared `inline`, hot library functions get rewrites instead of plain
inlining, and Convert onwards waits for the native backend. Select, Allocate
and Emit as described above are on their way out.
