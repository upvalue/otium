# Typed maps and multimethods

Status: prototype specification for review, October 16, 2026. These APIs do
not exist yet. The first implementation targets Otium on Chez.

The prototype should establish predictable method selection and interactive
redefinition. A future GUI library will use it for operations such as rendering,
measurement, and event handling. Building that library, choosing a renderer,
and optimizing UI updates are separate work.

## Scope and provisional choices

The agreed foundation is typed maps with stable descriptors, mutable fields,
built-in dispatch types, and generic functions whose methods can specialize
any subset of their arguments. Matching uses exact descriptor identity with
unspecialized arguments as fallbacks. Incomparable best matches are errors.

This draft also proposes concrete names, fixed arity, symbol-only field keys,
a built-in type inventory, and reload behavior. Those details are reviewable
choices, not previously settled requirements. Examples use Chez reader syntax;
they do not assume the C dialect's table literals, keywords, or nil semantics.

There is no inheritance, predicate dispatch, value specialization, method
combination, next-method call, optional argument, or rest argument support in
this version. There is no reactive field tracking. Dispatch caches are deferred.

## Type descriptors

`(define-type Button)` declares a descriptor in the current namespace.
Descriptors compare by identity. Their namespace-qualified names are diagnostic
labels, not lookup keys for dispatch. Two declarations in different namespaces
create distinct descriptors even if both use the local name `Button`.

Reevaluating the same declaration preserves its descriptor. Existing objects
therefore continue to match methods after source reload. If the local binding
already holds anything other than the descriptor belonging to that declaration,
the declaration fails. It must not silently reuse an unrelated descriptor.
Declaring over an imported binding also fails. Types may be imported through
the existing namespace mechanism and referenced through qualified aliases.

Descriptors are opaque and immutable. The prototype provides `type-descriptor?`
and `type-name`; the latter returns the diagnostic qualified symbol. There is
no public fresh-descriptor procedure or object retyping operation. Explicit
declaration reset is described below.
Deleting or renaming a declaration does not migrate existing objects.

## Typed maps

```scheme
(define-type Button)
(define button (make Button 'label "7" 'enabled? #t))
(field-ref button 'label)                 ; => "7"
(field-set! button 'label "8")
(dispatch-type button)                   ; => Button descriptor
```

`make` is an ordinary procedure. It accepts a user-defined descriptor followed by alternating symbol keys
and values. It creates a fresh object with that fixed descriptor and mutable
fields. Odd field argument counts, non-symbol keys, and built-in descriptors
are errors. Repeated keys keep the last value. Argument evaluation follows ordinary Chez procedure-call semantics.

`field-ref` accepts an optional default. A missing field without a default is
an error. `field-has?` distinguishes absence from any stored value, including
`#f` and the empty list. `field-set!` inserts or replaces a field and returns
the assigned value. `field-delete!` returns whether a field was present.
`field-keys` returns a fresh list of the currently present symbol keys, in
unspecified order. Mutating that list does not alter the object. Field access
and mutation require a typed map and a symbol key. There are no reserved
field keys: a field named `type` cannot change the descriptor.

`typed-map?` recognizes these objects. Fields have no declared schema or required
set. Two separately constructed objects have distinct identity even if their
descriptors and fields agree. No structural object equality or serialization
contract is added. The backing table is private, and field iteration order is
unspecified. A custom printed representation may show the type name; it need
not be readable as source.

On Chez, the initial representation is an opaque record with immutable
descriptor and entries slots. Entries refer to a mutable `eq?` hash table.
Reading an object's descriptor is direct record access. It does not perform a
field lookup. Native `hashtable-ref` continues to accept native hash tables;
a typed map is not itself a native hash table.

This representation may change without changing the field or dispatch APIs.
Fixed field layouts, constructors with defaults, and schema validation are
outside the prototype.

## Built-in dispatch identities

`dispatch-type` returns exactly one stable descriptor for every value. It checks
typed maps first, returning their attached descriptor. The remaining prototype
categories are disjoint:

| Exported descriptor | Chez values |
| --- | --- |
| `Number` | All numbers, including exact, inexact, and complex numbers |
| `Boolean` | `#t` and `#f` |
| `Character` | Characters |
| `String` | Strings |
| `Symbol` | Symbols |
| `Null` | The empty list |
| `Pair` | Pairs, including nonempty lists |
| `Vector` | Ordinary vectors |
| `Bytevector` | Bytevectors |
| `Hashtable` | Native Chez hash tables |
| `Procedure` | Procedures, including generic functions |
| `TypeDescriptor` | User-defined and built-in descriptors |
| `Other` | All remaining values |

These names are exports of the `otium.generic` Otium namespace, usable through
namespace aliases. They do not introduce a new numeric tower or replace Chez predicates.
There is no `Any` type descriptor: an unspecialized parameter expresses that
match. A typed map does not also match `Hashtable`; there is no subtyping.
Foreign Chez record types fall into `Other`. Supporting their distinct record
identities is deferred.

## Generic declarations and methods

```scheme
(define-generic (handle-event! widget event))

(define-method (handle-event! widget event)
  'ignored)

(define-method (handle-event! (widget Button) event)
  'button-default)

(define-type PointerDown)
(define-method (handle-event! (widget Button) (event PointerDown))
  'pressed)
```

A generic has one fixed arity, declared explicitly. Parameter names in its
declaration are documentation only. Zero-argument generics are permitted.
A generic is an ordinary callable procedure and works with `apply` and other
higher-order functions. Calling it with the wrong arity fails before selection.

In a method definition, a bare identifier is an unspecialized parameter.
`(parameter Type)` specializes that position on a descriptor binding, resolved
once when the method is registered. `Type` is a local or qualified identifier,
not an arbitrary predicate or dispatch expression. A non-descriptor is an error.
The method target must resolve to a generic; any other value is an error.
Parameter names must be distinct, and method arity must equal generic arity.
The body has ordinary lexical scope and receives all original arguments.
Return values, including multiple values, and raised conditions propagate
without conversion.

Methods can extend imported generics. A method definition changes the generic's
registry, not its namespace binding. Method identity is the ordered signature
of descriptor identities and wildcard positions; names of parameters and the
namespace registering the method do not affect that identity.

Registering the same signature replaces its implementation. Different
signatures coexist. Replacement is explicit behavior even across namespaces;
the prototype has no method ownership or extension conflict protection.

## Selection

For actual argument types `T[0..n)`, a method is applicable when every signature
position is either unspecialized or identical to the actual descriptor there.

Method A is more specific than method B if each position of A is the same as
B or replaces B's wildcard with an exact descriptor, and at least one position
makes that replacement. The number of specialized positions alone does not
establish precedence.

Collect applicable methods and discard every method dominated by a more
specific applicable method. Exactly one remaining method is invoked. No
remaining methods is a missing-method error. More than one is an ambiguity
error. Source order and registration order never break ties.

For an actual `(Button, PointerDown)` call:

| Registered signatures | Result |
| --- | --- |
| `(any, any)` | Invoke the fallback |
| `(any, any)`, `(Button, any)` | Invoke the Button method |
| `(Button, any)`, `(any, PointerDown)` | Ambiguity error |
| Both partial methods plus `(Button, PointerDown)` | Invoke the exact method |

Ambiguity is checked at invocation. Registration of potentially overlapping
methods is allowed. A fully unspecialized method is optional; there is no
implicit default result.

## Reload, mutation, and inspection

Reevaluating a generic declaration preserves its callable identity and methods
when the declaration belongs to the same namespace binding and has the same
arity. Changing arity or colliding with another value is an error. Saved generic
procedure references therefore see later method replacements.

For deliberate recovery or incompatible changes, provide two top-level forms:

```scheme
(reset-type! Button)
(reset-generic! (handle-event! widget event))
```

A reset requires an existing local definition and rejects imported bindings.
It may replace an unrelated value occupying that local binding. Resetting a
built-in descriptor export is prohibited. Validation finishes before replacement.
`reset-type!` installs a fresh descriptor owned by that declaration;
`reset-generic!` installs a fresh generic of the supplied arity with no methods.
Subsequent ordinary declarations preserve the new identity.

Reset is intentionally explicit. Saved references to the old generic keep its
old registry; old objects keep their old descriptor. Existing method signatures
retain their captured descriptors. Namespace imports resolve the replacement
binding through Otium's existing live alias behavior. Reset does not migrate
objects, rewrite signatures, or update saved procedure values. Both forms return
an unspecified value. This is a recovery mechanism, not routine reload behavior.

`generic?` recognizes generic procedures. `generic-methods` returns a fresh
list of signatures, each represented as a fresh list with descriptors for exact
positions and `#f` for wildcards. Order is unspecified; mutating these returned
lists must not alter the registry.

`remove-method!` takes a generic and a signature in that format. It validates
arity and entries, removes only the exact registered signature, and returns
whether one existed. A malformed signature is an error.

Removing a definition from a source file does not remove its registered method
on reload. Use `remove-method!` explicitly. Automatic namespace unload and
transactional reload are deferred. Successfully evaluated declarations remain
in effect if a later form in the reload fails.

Changing a signature in a source file leaves the old signature registered. For
example, replacing `(Button, any)` with `(any, PointerDown)` can introduce an
ambiguity after reload. Ambiguity diagnostics must list the conflicting
signatures and explain how to inspect `generic-methods` and call `remove-method!`.
They must not guess which signature is stale. Method ownership and automatic
removal of definitions absent from a reload remain deferred.

A call selects one implementation before invoking its body. Registry changes
made during that body affect subsequent calls, including recursive calls, but
do not replace the body already executing. This prototype specifies sequential
use; concurrent registration and invocation are outside its scope.

## Errors

Use Chez conditions catchable with `guard`. Distinguish invalid declarations,
wrong call arity, missing methods, and ambiguous calls in the message and
irritants. Dispatch failures identify the generic, actual type names, and, for
ambiguity, all remaining candidate signatures. Avoid printing argument contents
just to explain dispatch. No new restart or condition hierarchy is required.

Prototype-generated errors have a stable symbol as their first condition
irritant: `invalid-declaration`, `binding-conflict`, `not-generic`,
`invalid-specializer`, `invalid-signature`, `wrong-arity`, `missing-method`,
`ambiguous-method`, `invalid-field-operation`, or `missing-field`.
Tests assert these codes rather than English wording. Remaining irritants carry
the relevant names, signatures, or field keys. Conditions raised by a method
body or ordinary expression evaluation are propagated unchanged.

Declaration and registration validation must finish before mutating a registry.
A failed replacement leaves the previous method available.

## Implementation boundary

Start with a linear method scan and the specificity rule above. There is no
performance claim for the prototype. Keep selection separate from invocation
so it can be tested without a GUI. A later cache can key on descriptor tuples
and invalidate on registry changes, but is not part of this milestone.

The implementation can use Chez records for descriptors and typed maps, and
closures for generic procedures. An identity registry can associate generic
closures with their method tables; it must not permanently retain otherwise
unreachable generics. An alternative private closure protocol is acceptable
if it preserves normal application behavior.

Use a private Chez library `(otium generic-core)` for descriptors, typed maps,
and registries. Expose ordinary public procedures and built-in descriptors
through the Otium namespace `otium.generic`. Support default imports, `:as`,
and `:refer` without requiring any particular alias.

For this prototype, implement `define-type`, `define-generic`, `define-method`,
`reset-type!`, and `reset-generic!` as loader-supported core declarations, like
`defonce`. They are available without importing syntax. This is a deliberate
exception to the preference for library macros, subject to review before
implementation. It avoids adding caller-namespace reflection or reserved helper
bindings to every environment just to support these declarations.

The top-level coordinator knows the declaration's actual namespace. It uses
that namespace and local name as the declaration owner and diagnostic name.
It resolves user descriptor and generic references in that environment, expands
method bodies through the existing expander, compiles the resulting lambda in
the same environment, and invokes private registration helpers directly. It
must not emit helper references that depend on the caller's import aliases.
Do not bypass Otium body expansion with raw Chez `eval`.

Type and generic declarations perform a create-or-validate operation. An
existing definition must have the expected kind and declaration owner, and a
generic must also have the expected arity. Merely expanding to `defonce` is
insufficient because it skips validation of an existing value. Successfully
created definitions enter the existing namespace definitions registry, making
them available to `require`, `:refer`, and qualified aliases. Imported-binding
collisions are checked before creation or reset.

These forms are permitted only at namespace top level, including top-level
`begin` and macro-generated top-level forms. Extend the expander's core-form
handling to reject them in expression or local-definition positions, including
`define-method`. Preserve the existing lexical-shadowing rules for ordinary
procedure calls. Rejection must occur even in an unexecuted function body.
Read `chez/MACRO-DESIGN.txt` and the loader when implementing these changes.

Do not change the C runtime or implement a GUI as part of this prototype.

## Acceptance examples and checks

The implementation should include a headless Button/PointerDown example and a
generic with methods for `(Number, Number)` and `(String, String)`. This exercises
user-defined and built-in identities without a rendering dependency.

Tests must establish:

1. Fresh typed maps share their declared descriptor but have independent fields;
   false values, missing fields, deletion, and invalid constructor input behave
   as specified.
2. Each built-in category returns a stable descriptor; a typed map does not match
   a method specialized on `Hashtable`.
3. Exact matches, partial fallbacks, full fallbacks, missing methods, and
   incomparable candidates follow the selection rule for one, two, and three
   arguments. Registration order does not affect selection.
4. Exact replacement does not create ambiguity. Removal exposes a fallback or
   an error as appropriate. Failed registration leaves existing behavior intact.
5. Saved generic references see replacements, while old objects continue matching
   after their type and generic declarations are reevaluated.
6. Two namespaces with the same local type name remain distinct; another
   namespace can register a method on an imported generic using an imported type.
7. Wrong arity, invalid specializers, duplicate parameter names, and binding
   collisions report errors. A method targeting a non-generic reports
   `not-generic`. Error-code assertions do not depend on message wording.
   Zero-argument calls work.
8. Bodies receive original objects; return values and conditions propagate.
   Introspection cannot mutate the registry. Recursive calls observe completed
   registry updates. Field-key enumeration is a detached snapshot.
9. Declarations work with default imports, `:as`, and `:refer`, and when generated
   by macros at top level. Local declarations are rejected consistently.
10. Explicit resets recover incompatible declarations without migrating old
    objects, saved generics, or captured signatures. Imported resets fail.
11. A signature-changing reload retains the old method and produces the specified
    ambiguity diagnostic when applicable; explicit removal resolves it.

Run the focused Chez tests and namespace/macro regressions. UI tests and C
collector lint are not relevant until those implementations are changed.

## Review questions

Is wildcard specificity sufficient for the first GUI examples without bringing
in inheritance? Does explicit generic arity keep the implementation small? Are
the proposed loader-supported declarations a worthwhile exception to the
preference for library macros? Do the proposed built-in categories, field API, and reload rules expose
surprises that should be resolved before coding?

The arbitrary-dispatch-function model in
[Clojure multimethods](https://clojure.org/reference/multimethods) is background
for this discussion. This proposal instead fixes dispatch to argument type
identities and exact matches with wildcards.
