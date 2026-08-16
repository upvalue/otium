---
id: lan-pzh6
status: open
deps: []
links: [lan-ygfd, lan-dm0j, lan-iuyk, lan-d41h]
created: 2026-08-16T20:04:23Z
type: task
priority: 1
assignee: Phil
tags: [gc, compiler, code-quality, refactor]
---
# Make the rooted stack the only way to name a heap value: Ref handles, stack-returned results, Status control flow

vm->stack currently does three jobs at once: the VM operand/local stack (CallFrame callBase/base/stackBase, opcode push/pop), a shadow stack for rooting C locals (scope_begin/scope_push/Slot/scope_pop_to, ~150 sites in compile.c alone), and alongside it heap.tempRoots is a separate third root vector used to root make_* arguments.

Natives are already half-converted to the Lua convention on the argument side: nat_add(vm, base, argc) reads its arguments through ARG(n) out of rooted stack slots. But everything returns a raw Value, so the stack is a supplementary rooting device rather than the actual interface. We pay Lua's ergonomic cost (index math, base snapshots, a manual pop on every exit path) without getting Lua's guarantee, which is that an unrooted reference is unrepresentable.

The result is that GC safety at any given call site is a whole-program argument that has to be re-derived by hand. emit_let (compile.c:690) holds a raw `Value body` across body_define_name and bind_next_slot. That is safe today, but only because bind_next_slot happens not to allocate on the GC heap. Nothing catches it if that changes.

Two open tickets are symptoms of this rather than independent bugs. lan-dm0j: compiler_error raises (which allocates) and then returns so emission continues, leaving any raw Value live in an emit helper's error path stale, and the following array_push can store a dangling Obj* into the rooted constants pool. lan-ygfd: the same rooted reverse-fold is re-derived in five places because there is no way to express it once over rooted operands.

Two more tells. scope_pop_to is defensively guarded (`if (vm->stack.len > base)`) because an enclosing VM unwind may already have popped below base, which is an admission that strict nesting between C scopes and VM frames is not actually provable. And OT_TRYS exists only to pop a scope on the unwind path, i.e. it is a hand-rolled unwinding destructor left over from the C++ RAII guard this was transliterated from (see the comment at compile.c:419).

## Design

Value is doing two unrelated jobs, both hand-rolled: a possibly-heap reference carrying an unenforced rooting obligation, and an error monad, since Tag_Unwind is a value tag used as a control-flow signal (which is the only reason OT_TRY/OT_TRYS exist). Fixing both at once collapses them into a single convention.

## Handles

A distinct type naming a rooted stack cell, not convertible to or from Value:

    typedef struct { u32 i; } Ref;
    static inline Value ref_get(State*, Ref);
    static inline void  ref_set(State*, Ref, Value);

Drop the State* that Slot carries today. It is 8 redundant bytes on a type that is about to be everywhere, and the vm is always in scope at the use site anyway.

## Results on the stack, control flow in the return value

    [[nodiscard]] Status compile_lambda(State* vm, LambdaInfo*, Ref body, u32 name);
    // Ok      => exactly one value pushed
    // Unwind  => stack restored to entry depth

Status is a two-valued enum. [[nodiscard]] makes a dropped error a compiler diagnostic instead of a silent miscompile. OT_TRY becomes a plain if, and OT_TRYS goes away.

## Arguments are Ref, never Value

This is the part that does the real work. A transient like car_(slot_get(cursor)) cannot be passed to a function that might allocate, because the types do not line up. The emit_let hazard becomes unrepresentable rather than merely absent.

## Assert the depth invariant

Entry/exit stack-depth checks in debug builds on every function following the convention, the way Lua uses api_check. This is what turns the convention into something the test suite enforces instead of something review has to catch, and it is probably worth more than the syntactic cleanup.

## One stack, and tempRoots deletes

heap.tempRoots goes away entirely: once make_pair(vm, Ref, Ref) takes rooted arguments there is nothing left to root internally. One mechanism, one rule, which is that a heap value lives on the stack. lan-ygfd's fold then gets written once over a Ref range.

Keep a single stack rather than splitting the C root stack from the VM operand stack. Splitting was considered and is the wrong call here: under this convention the C code follows the same discipline as the VM (push operands, call, result replaces), which is exactly why Lua has one stack. The defensive guard in scope_pop_to becomes a hard assert.

## Scope of the churn

Smaller than it first looks. The rule is only "if you can return a heap value, you return it on the stack", so anything returning immediates is untouched: all of arith.c, the comparisons, the predicates. Natives only change on the return side. compile.c (1554 lines) is the bulk of the work, then eval.c, state.c, ns.c, reader.c, and the heap-returning parts of builtins/.

## Open questions

1. Does Tag_Unwind leave the Value representation entirely, or stay as a VM-internal sentinel? Removing it is cleaner and shrinks the tag switch, but a few places use unwind_v() as an in-band signal (global_cell returns it for an unresolved symbol) and each needs a real return channel. Separable from the rooting work, so it could be a follow-on ticket instead of part of this one.

2. Ref as a bare index vs. keeping the State* in the handle. Bare index is smaller, but it means ref_get(vm, r) at every use rather than slot_get(r), and it loses the (weak) protection against mixing two States.

Neither is decided yet.

## Acceptance Criteria

- No function in src/ takes a heap-capable Value as a parameter or returns one. Heap results are pushed; the return value carries only Ok/Unwind. Immediate-only functions (arith, comparisons, predicates) are exempt and unchanged.
- Slot and scope_begin/scope_push/scope_pop_to/scope_exit are gone, replaced by Ref plus a scope whose exit is automatic.
- OT_TRYS is gone. OT_TRY is a plain nodiscard status check.
- heap.tempRoots is deleted and heap.h no longer declares a second rooting mechanism.
- Debug builds assert entry/exit stack depth on every function following the convention, and the test suite runs with those asserts on.
- The guard in scope_pop_to is a hard assert rather than a silent clamp.
- lan-dm0j is fixed by construction: no raw Value can be live across compiler_error.
- lan-ygfd's fold exists once and the five hand-rolled copies are gone.
- Existing test suite passes, and the r7rs benchmarks (lan-70gb) are re-measured against the pre-rewrite numbers so any regression from the extra stack traffic is visible.


## Notes

**2026-08-16T21:39:40Z**

Done except Status adoption. Ref is a bare index -- the stack still moves, so pointer handles are out. OT_SCOPE via cleanup attr; one-per-function, enforced by -Wshadow. Open question 1 (Tag_Unwind out of Value) not taken up.

**2026-08-16T21:41:07Z**

Ref/OT_SCOPE done across src/. Remaining: Status adoption at the Value-returning boundaries.
