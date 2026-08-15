# The Otium Language

**Specification -- draft 0.3**

Otium is a small Lisp: a Scheme kernel made pragmatic for embedding and
data munging. The choices that make it itself:

- **Lua's `nil`.** A distinct "absence" value that is falsy, deletes table
  keys, and threads through lookup pipelines. `#f` stays the honest boolean
  false, and `()` is the (truthy) empty list.
- **First-class mutable arrays and tables** with literal syntax (`[...]`,
  `{...}`), callable as lookup functions, insertion-ordered.
- **Clojure-style namespaces** of mutable *vars*. Redefinition is instantly
  visible through every existing reference; live redefinition is a core
  language property, not a debugging trick.
- **Common-Lisp-style conditions.** Handlers run at the signal site before
  the stack unwinds, and named *restarts* let a handler resume execution at
  a chosen recovery point.
- **Unhygienic macros** with `gensym`, elisp/Janet style.
- **Proper tail calls** and cooperative interruption as semantic
  requirements, so a host can run long scripts and cancel them safely.

This document is meant to be sufficient to implement the language from
scratch: reader syntax, the data model, evaluation rules, every special
form, and every binding in the core library. `otium.core` includes forms
defined in the bootstrap prelude; an implementation may define them
natively or in Otium itself, and the difference is not observable.

Words like *error* below mean: a condition is raised as if by the `error`
function (see section 8), with an implementation-chosen message.

---

## 1. Lexical syntax

Source text is UTF-8. The reader converts text into values; programs are
sequences of values evaluated in order.

### 1.1 Whitespace and comments

Any Unicode whitespace separates tokens. `;` begins a comment that runs to
end of line. There is no block comment.

### 1.2 Delimiters

The characters `( ) [ ] { } ; " , ' `` ` `` ` terminate an atom and can
never appear inside one. Everything else may appear in a symbol, including
`.` (subject to the dotted-pair rule in 1.6).

### 1.3 Atoms

An atom token is classified in this order:

1. `nil` is the nil value.
2. `#t`, `#true`, `true` are boolean true; `#f`, `#false`, `false` are
   boolean false.
3. Any other token starting with `#` is a read error. The `#` prefix is
   reserved.
4. `:name` is the keyword `name` (any non-empty run of atom characters).
   A bare `:` is a read error.
5. A token that *starts numerically* -- optionally signed (`+`/`-`), then a
   digit, or a `.` followed by at least one more character -- must parse as
   a number, otherwise it is a read error. `12abc` is an error, not a
   symbol. The number forms:
   - decimal integers: `42`, `-7` (width per section 2; a literal outside
     the implementation's int range is a read error)
   - hexadecimal integers: `0xff`
   - floats (IEEE 754 double), including `3.5`, `-.5`, `1e3`
6. Anything else is a symbol: `foo-bar!`, `+`, `->`, `term/write`, `/`.

Symbols containing `/` may be treated as namespace-qualified at resolution
time (7.3). That is a property of evaluation, not of the reader; the
reader produces an ordinary symbol.

### 1.4 Strings

`"..."` with escapes `\n` `\t` `\r` `\0` `\e` (escape, U+001B) `\"` `\\`.
Any other escape is a read error. Strings may span lines. Strings are
immutable sequences of Unicode characters (code points).

### 1.5 Quote sugar

| sugar | reads as |
|---|---|
| `'x` | `(quote x)` |
| `` `x `` | `(quasiquote x)` |
| `,x` | `(unquote x)` |
| `,@x` | `(unquote-splicing x)` |

### 1.6 Lists and pairs

`(a b c)` reads as a proper list; `()` reads as the empty list. A `.`
standing alone (followed by whitespace or a delimiter) marks a dotted
tail: `(a . b)` is a pair, `(a b . c)` an improper list. A dotted tail
with no preceding element, or content after the tail, is a read error.

### 1.7 Collection literals

`[a b c]` reads as the list `(array a b c)` and `{k v ...}` as
`(table k v ...)`. These are plain applications of the core constructors,
so elements are evaluated and the literals nest and quasiquote like any
other form. It follows that `'[1 2]` is the *list* `(array 1 2)`, not an
array. Contexts that accept "a list of symbols" (parameter lists, `:refer`
specs) also accept the bracketed form by ignoring a leading `array`
symbol.

---

## 2. Types and values

| type name | printed (repr) | mutable | notes |
|---|---|---|---|
| `nil` | `nil` | -- | absence; falsy |
| `null` | `()` | -- | the empty list; truthy |
| `boolean` | `#t` / `#f` | -- | |
| `int` | `42` | -- | signed, implementation-defined width (≥ 31 bits); overflow wraps (Appendix A) |
| `float` | `3.5`, `3.0` | -- | IEEE 754 double |
| `symbol` | `foo` | -- | |
| `keyword` | `:foo` | -- | self-evaluating |
| `string` | `"hi"` | -- | Unicode; indexed and measured by code point |
| `pair` | `(1 . 2)` | -- | **immutable**, so list structure can never be cyclic |
| `array` | `[1 2]` | yes | growable vector |
| `table` | `{:a 1}` | yes | insertion-ordered map, arbitrary keys |
| `buffer` | `#<buffer "…">` | yes | mutable string builder |
| `function` | `#<fn name>` | -- | closures and host natives are indistinguishable |
| `macro` | `#<macro name>` | -- | |
| `param` | `#<param name>` | -- | dynamically-scoped cell (section 9) |
| `restart` | `#<restart name>` | -- | active recovery point (8.5); identity compare |

`(type v)` returns the type name above as a keyword.

Implementations may add opaque host types. They must print as
`#<something>`, compare by identity, and be rejected by the type
predicates in 10.2.

### 2.1 Truthiness

Exactly two values are falsy: `nil` and `#f`. Everything else is truthy,
including `()`, `0`, `0.0`, and `""`.

### 2.2 nil

`nil` means *absence* and is not a storable table value. Looking up a
missing key yields `nil`, and storing `nil` under a key **deletes** the
key (in `table`/`{...}`, `put!`, `assoc`, `merge`). "Present with nil" is
unrepresentable. Sequence functions treat `nil` as an empty sequence, and
lookups on `nil` yield `nil`, so access chains compose over misses.

The rule behind the library's treatment of `nil`, and the razor for
extending it: **read operations are total over absence** -- `count`,
`first`, `map`, `get`, and their kind see `nil` as empty and never error
on it. A function whose contract implies a particular kind builds that
kind from `nil` (`assoc` builds a table, `rest` returns a list); that is
the function knowing what it makes, not `nil` being a collection. A
kind-*preserving* function given `nil` has no kind to preserve and
returns `nil` (`copy`). And where absence cannot mean anything --
`(car nil)`, arithmetic -- it is an error, not a zero.

### 2.3 Table ordering

Tables remember the order their keys were first inserted, and every
order-observing operation uses it: printing, `keys`, `values`, and the
iteration behind `merge`, `map-vals`, `group-by`, and `frequencies`. The
fine print:

- Updating an existing key keeps its original position.
- Deleting a key and inserting it again moves it to the end; the
  re-insertion is a fresh first insertion.
- Order does not participate in equality. Tables compare by identity
  (2.4); if a structural table comparison is ever added, whether it
  ignores order is a separate decision to make then.

The `{...}` constructor and `merge` process their arguments left to
right, so the result's order is first-seen order across the inputs.

### 2.4 Equality

Three notions, coarsest to finest:

- **`=`** -- numeric equality across int/float: `(= 1 1.0)` is `#t`.
  Arguments must be numbers.
- **`equal?`** -- deep structural equality for immutable data (pairs
  recursively, strings, symbols, keywords, numbers *of the same type*),
  identity for mutable objects (arrays, tables, buffers) and functions.
  Type-strict: `(equal? 1 1.0)` is `#f`, and `nil`, `()`, `#f` are all
  distinct. For the benefit of table keys, `NaN` equals `NaN` and `0.0`
  equals `-0.0`.
- **`eq?`** -- identity: pointer identity for pairs, strings, and all
  mutable heap objects; same as `equal?` for immediates (numbers,
  booleans, `nil`, `()`). Symbols and keywords are interned, so `eq?`
  on equal ones is `#t`. Two equal strings are `eq?` only when they are
  the same object; whether an implementation ever shares string objects
  (equal literals, say) is unspecified, so compare strings with
  `equal?`.

Table keys are compared with `equal?` semantics. A hashing implementation
must therefore hash immutables structurally and mutables by an identity
that is stable across GC -- a stored id, never an address.

### 2.5 Ordering

Only numbers order under `<` `>` `<=` `>=` (mixed int/float compares
numerically). `sort`/`sort-by` additionally order strings, symbols, and
keywords lexicographically, but the keys in one sort must all be of one
class (numbers / strings / symbols / keywords); mixing classes is an
error. Sorting is stable.

### 2.6 Printed representations

Every value has two renderings:

- **repr** (used by `write`, the REPL, and error messages): reads back
  where possible. Strings are quoted and escaped; floats always carry a
  decimal point or exponent (`3.0`, not `3`).
- **display** (used by `display`/`print`/`println`/`str`): identical
  except strings and buffers render as their raw characters, recursively
  inside collections.

### 2.7 Complexity guarantees

Programs may rely on the following costs. `n` is the element count of the
collection involved unless stated otherwise; "expected" means a hashed
operation whose worst case may degrade under adversarial keys.

**Pairs and lists** are immutable singly-linked chains:

- `cons`, `car`, `cdr`, `first`, `rest`: O(1)
- `length`, `nth`, `last`, `reverse`, `list?`: O(n)
- `append`, `concat`: O(total length of the arguments)

**Arrays** are contiguous vectors:

- indexed `get` / `put!`, `length`, `count`, `pop!`: O(1)
- `push!`: amortized O(1)
- `copy`: O(n)

**Tables** are hashed, insertion-ordered maps:

- `get`, insertion via `put!`, `contains?`: expected O(1). Hashing a key
  costs O(size of the key) for structural (immutable) keys, O(1) for
  mutable objects (identity).
- deletion (storing `nil`): amortized O(1). Preserving insertion order
  does not excuse O(n) deletes -- the intended layout is a compact entry
  vector with tombstones, compacted when tombstones dominate, which
  amortizes to O(1) per delete.
- `length`, `count`: O(1); iteration, `keys`, `values`, `copy`: O(n)

**Strings** are immutable and indexed by code point; an implementation
may store them as UTF-8, so `string-length`, indexed `get`, and
`substring` may each cost O(length of the string). Concatenation is
O(total length). **Buffers** exist to make repeated appending cheap:
`buffer-push!` is amortized O(length appended), `buffer->string` O(n).

**Equality:** `eq?` is O(1). `equal?` is O(size of the smaller value)
(deep for immutable structure, O(1) once it reaches mutables).

**Library functions** over sequences (`map`, `filter`, `reduce`,
`group-by`, `frequencies`, …) are O(n) in elements visited, times the
cost of their callbacks; `group-by` and `frequencies` add expected-O(1)
table operations per element. `sort` and `sort-by` are O(n log n) and
stable. `assoc`, `dissoc`, and `update` copy: O(n) plus the operation.
`contains?` on a sequence is O(n); on a string, naive substring search
(O(n·m)) is permitted.

Proper tail calls run in O(1) stack (3.5).

---

## 3. Evaluation

A program is a sequence of top-level forms evaluated in order.

- Self-evaluating: everything except symbols and non-empty lists
  (numbers, strings, keywords, booleans, `nil`, `()`, arrays, tables,
  functions, …).
- A **symbol** evaluates to the value of its binding, found by the
  resolution order of 3.1. Unresolvable symbols are an error.
- A **non-empty list** is evaluated by the rules of 3.2.

### 3.1 Symbol resolution

For an unqualified symbol (no interior `/`):

1. the innermost lexical binding (function parameters, `let`, body-level
   `define`), searching outward through enclosing scopes;
2. the current namespace's own vars;
3. the current namespace's referred vars (section 7).

For a qualified symbol `p/n` (interior `/`, neither side empty; `/` alone
is unqualified): resolve `p` through the current namespace's alias table,
falling back to `p` as a literal namespace name, then look up `n` in that
namespace's own vars. Missing namespace, missing var, or a private var
referenced from another namespace (7.4) is an error. Qualified symbols
never resolve lexically.

Free symbols in a function body resolve in the function's *defining*
namespace at call time (7.5).

### 3.2 List evaluation

Evaluation operates on fully macroexpanded forms: expansion (section 6)
rewrites macro calls away before a form is evaluated, so the evaluator
only ever sees special forms and applications.

For a list `(head arg…)`:

1. If `head` is a symbol naming a **special form** (section 5), the
   form's own rule applies. Special forms are recognized by name before
   any resolution and cannot be shadowed, lexically or by definition.
2. Otherwise `head` is evaluated, then the arguments left to right, and
   the result is applied (3.3).

The argument portion must be a proper list; a dotted call form is an
error.

### 3.3 Application

Callable values:

- **Functions** -- bind parameters (3.4) and evaluate the body forms in
  sequence in a fresh scope. The last form's value is the result (`nil`
  for an empty body), and the last form is in tail position.
- **Tables and arrays** -- `(coll key)` / `(coll key default)` behave as
  `get` (10.5): `nil` on a miss unless `default` is given.
- **Keywords** -- `(:k coll)` / `(:k coll default)`: lookup with the
  keyword as key.
- **Params** -- `(p)` returns the param's current dynamic value
  (section 9). Calling one with arguments is an error.

Applying anything else is an error.

### 3.4 Parameter lists and arity

A parameter list is one of:

- `(a b c)` -- fixed parameters;
- `(a b . rest)` or `(a b & rest)` -- trailing rest parameter, bound to a
  *list* of the remaining arguments (possibly `()`);
- a bare symbol `args` -- all arguments as a list;
- the array-literal spelling `[a b]` of any of the above (1.7).

Calling with fewer arguments than the fixed parameters, or more when
there is no rest parameter, is an error. There are no optional or keyword
parameters.

### 3.5 Tail calls

Implementations must run iterative recursion in constant stack: a call in
tail position reuses the current activation. Tail positions are: the last
body form of a function, `let`, `begin`/`do`; both branches of `if`; the
last form of `and`/`or`; the body of a chosen `cond` clause. Non-tail
recursion depth may be bounded (Appendix A); exceeding the bound is an
error.

### 3.6 Interruption

A host may interrupt a running program asynchronously. The interpreter
checks an interrupt flag at bounded intervals during evaluation; when
set, evaluation unwinds with the distinguished **quit** condition, which
`try`/`handler-bind` cannot intercept but `unwind-protect` cleanups do
run through. This is the only preemption in the language; there are no
threads.

---

## 4. Definitions and assignment

### 4.1 `define`

```
(define name expr)
(define name "docstring" expr)
(define (name . params) ["docstring"] body…)
```

At top level (outside any function body), `define` creates or updates a
**var** in the current namespace (7.2) and returns the value. The
function form is sugar for `(define name (lambda params body…))` and
names the closure; the value form also names a previously anonymous
closure bound to it. `def` is an alias. `define-` is identical but marks
the var **private** (7.4).

Inside a function or `let` body, `define` instead creates a *local*
binding in the current scope. Docstrings are accepted there but have no
observable effect.

A docstring is recognized when the body's first form is a string literal
followed by at least one more form. It is stored in var metadata and
surfaced by `describe`.

### 4.2 `set!`

`(set! name expr)` assigns to the nearest lexical binding of `name`, or,
if none exists, to `name`'s var (resolved as in 3.1, including qualified
names). Assigning an unbound name is an error. Returns the value.

---

## 5. Special forms

Special-form names are reserved: they are recognized syntactically and
cannot be rebound or shadowed.

**`(quote x)`** -- `x` unevaluated. Exactly one argument.

**`(if test then)` / `(if test then else)`** -- evaluates `test`; truthy
takes `then`, otherwise `else` or `nil`. Branches are tail positions.

**`(define …)` / `(def …)` / `(define- …)` / `(set! …)`** -- section 4.

**`(lambda params body…)`** -- a closure capturing the enclosing lexical
scope and the current namespace. `fn` is an alias. The body may begin
with a docstring (4.1).

**`(defmacro name params body…)`** -- defines `name` in the current
namespace as a macro (section 6).

**`(begin body…)`** -- evaluates in order, yields the last value; `nil`
when empty. `do` is an alias.

**`(let bindings body…)`** -- `bindings` is a list of `(name expr)`
pairs. Binding is **sequential**: each `expr` sees the bindings before it
(what Scheme calls `let*`). That is the only `let` the language has --
there is no parallel form and no `let*` alias. The body evaluates in the
new scope; an empty body yields `nil`.

**`(while test body…)`** -- re-evaluates `test` before each iteration;
runs the body while it is truthy. Returns `nil`.

**`(and forms…)`** -- left to right; returns the first falsy value, or
the last value; `(and)` is `#t`.

**`(or forms…)`** -- left to right; returns the first truthy value, or
the last value; `(or)` is `#f`.

**`(cond clauses…)`** -- each clause is `(test body…)` or `(else body…)`.
Tests evaluate in order; the first truthy one's body runs and its last
value is returned. A one-element clause `(test)` yields the test's value
itself. `else` is truthy in test position and requires a body. No clause
chosen yields `nil`.

**`(quasiquote x)`** -- template evaluation. `(unquote e)` at depth 1
evaluates `e`; `(unquote-splicing e)` as a list element at depth 1
evaluates `e` (which must be a proper list) and splices its elements.
Nested quasiquotes increment depth, unquotes decrement it, and only
depth-1 unquotes evaluate. `unquote` / `unquote-splicing` outside
quasiquote is an error, as is splicing a non-list.

**`(ns name clauses…)`**, **`(require spec…)`**, **`(in-ns name)`** --
section 7.

**`(handler-bind …)`, `(restart-case …)`, `(try …)`,
`(unwind-protect …)`/`(defer …)`** -- section 8.

**`(defparam …)` / `(with-params …)`** -- section 9.

---

## 6. Macros

A macro is a closure that runs at **expansion time**: it is called with a
call's argument forms unevaluated, and the form it returns replaces the
call.

Expansion is eager and happens once. Immediately before a top-level form
is evaluated, it is fully expanded:

- A list whose head is a symbol that is not a special-form name, is not
  lexically bound at that point, and resolves *as a var* to a macro is
  replaced by the result of calling the macro on the unevaluated argument
  forms; the replacement is then expanded in turn (its head may be
  another macro call). Lexical bindings shadow macros in head position:
  the expander tracks the bindings introduced by `lambda`, `let`, and
  body-level `define`, and a head symbol bound by one of them is a plain
  application, never a macro call.
- Any other form has its subforms expanded recursively, except positions
  the special forms treat as data: `quote`d forms, parameter lists,
  binding and clause names, `ns`/`require` specs. Inside `quasiquote`,
  only expressions under depth-1 `unquote`/`unquote-splicing` are
  expanded.

Expansion is per top-level form, in program order. The consequences:

- A macro must be defined before the top-level form that uses it is
  evaluated. Macros defined earlier in a file are available to the forms
  after them; forward references to macros are not possible.
- Redefining a macro affects only code expanded afterward. Definitions
  that were already expanded keep their old expansion until re-evaluated.
- Function bodies are expanded once, when the enclosing definition is
  evaluated -- not on each call.

Code constructed at runtime is expanded when it reaches `eval` (10.8).
The expander runs ordinary code: a macro body may call any function
already defined at expansion time.

Macros are unhygienic: introduced symbols capture whatever is in scope at
the expansion site. `(gensym)` / `(gensym prefix)` returns a fresh symbol
guaranteed distinct from any symbol appearing in source or from prior
gensyms. That, plus discipline, is the hygiene story.

Macros live in vars like any definition. A macro var referenced in
non-head position evaluates to the macro object itself (`macro?` tests
for it), which cannot be applied as a function.

---

## 7. Namespaces

### 7.1 Model

A **namespace** is a first-class runtime table of definitions: a map from
names to **vars**, plus an alias map (`local-name → namespace-name`) and
a refer map (`name → var of another namespace`). Namespaces are created
on demand and never destroyed.

A **var** is a mutable cell with metadata (name, owning namespace,
docstring, private flag). Every reference goes through the cell,
including refers in other namespaces, so redefinition is instantly
visible everywhere. Redefining reuses the existing cell.

There is always a **current namespace**; programs start in `user`. The
core library lives in `otium.core`, and every newly created namespace
automatically refers all public vars `otium.core` has *at creation time*.

### 7.2 `ns`, `in-ns`, and top-level switching

```
(ns my.name
  (:require (full.name :as short)
            (other.name :refer (a b))))
```

`ns` switches the current namespace to `my.name` (creating it if needed)
and processes clauses; only `:require` clauses exist. `(in-ns name)` just
switches (name may be a symbol, keyword, or string). Returns `nil`.

A namespace switch performed by a top-level form persists for subsequent
top-level forms. Anywhere else -- inside nested evaluation or a function
call -- the current namespace is restored when the enclosing form
finishes, so a switch cannot leak out of nested code.

### 7.3 `require`

`(require spec…)` where each spec is a symbol `full.name` or a list
`(full.name option…)` with options:

- `:as alias` -- register `alias → full.name` in the current namespace's
  alias map.
- `:refer (a b)` -- copy the named public vars into the current
  namespace's refer map. Referring a missing or private var is an error.
- `:reload` -- discard any loaded copy of the namespace and re-load it.

Quoting of specs and names is tolerated and ignored. If the namespace is
not yet loaded, it is located on the **load path**: the name with `.`
replaced by `/` plus the extension `.scm`, searched across the host's
configured load-path directories; the file is evaluated with the current
namespace set to the target. A file that is already mid-load (circular
require) is an error, as is a name not found on the path.

### 7.4 Privacy

`define-` marks a var private. Private vars resolve normally from their
own namespace but are an error to reach via qualified reference or
`:refer` from any other namespace, and are skipped by the automatic core
refer.

### 7.5 Namespace of execution

A closure remembers its defining namespace; during a call, free symbols
in the body resolve there, not in the caller's namespace.

---

## 8. Conditions and restarts

Otium's error system separates *signalling* a condition, *handling* it,
and *transferring control*. Handlers run at the signal site, before any
unwinding, and may either decline or unwind to a restart.

### 8.1 Conditions

A condition is an ordinary value. By convention it is a table with a
`:type` symbol and usually a `:message` string. The condition
constructors build exactly:

```
(error "msg")          ; raises {:type 'error :message "msg"}
(error "msg" a b)      ; … plus :data [a b]
(error value)          ; raises value as-is (any non-string)
```

`signal` builds conditions the same way. Passing extra arguments after a
non-string is an error. The helpers `condition-type`,
`condition-message`, `error?`, and `type-pred` (10.9) read this
convention.

Condition types form a hierarchy. `(define-condition type parent)`
registers `type` (a symbol) as a subtype of `parent` in a
runtime-global registry; both arguments may be symbols, keywords, or
strings, and are taken as symbols. Re-registering a type replaces its
parent; a registration that would create a cycle is an error. The type
`error` is predefined as a root; new roots may be made by
`(define-condition type nil)`. `(condition-of-type? c type)` is `#t`
iff `(condition-type c)` is `type` or reaches it by walking parents.
`(type-pred t)` returns `(lambda (c) (condition-of-type? c t))`, so a
handler predicate for `error` catches a `file-error` defined under it.
Types never mentioned in a registration simply have no parent; they
still match themselves.

### 8.2 Signalling

`(signal c)` walks the dynamically active handler bindings from innermost
to outermost. For each binding `(pred handler)` whose `pred` returns
truthy on `c`, `handler` is called with `c` **at the signal site** -- the
stack has not unwound. While a handler frame runs, that frame and all
frames inner to it are invisible to further signals, so a handler
signalling does not recurse into itself. A handler *declines* by
returning normally; the walk continues and its return value is ignored.
A handler exits non-locally by invoking a restart or raising. If every
handler declines, `signal` returns `nil`.

`(error …)` is `signal` followed, if every handler declines, by unwinding
with the condition: it never returns. An unwinding condition propagates
outward until caught by `try` or it reaches the host.

The **quit** condition (3.6) never passes through handlers and is not
catchable by `try`; only `unwind-protect` cleanups observe its passage.

### 8.3 `handler-bind`

```
(handler-bind ((pred handler) …) body…)
```

Evaluates the predicate and handler expressions (each to a callable),
then runs the body with those bindings active, innermost-last. Returns
the body's value. The bindings are active for signals raised anywhere in
the body's dynamic extent, including inside functions it calls.

### 8.4 `restart-case` and `invoke-restart`

```
(restart-case expr
  (name ["description"] (params…) body…)
  …)
```

Establishes named **restarts** for the dynamic extent of `expr`. If
`expr` returns normally, its value is returned and the restarts
disappear. The optional description is a string literal meant for
humans -- a host presenting restarts as a prompt shows it next to the
name. It is recognized like a docstring: a string in that position
followed by the parameter list.

`(invoke-restart 'name args…)` (name may be symbol/keyword/string) finds
the innermost active restart with that name -- dynamically, not
lexically -- and unwinds the stack to its `restart-case`, running any
intervening `unwind-protect` cleanups. It then evaluates the chosen
clause's body with `params` bound to `args`, and the clause body's value
becomes the value of the whole `restart-case`. Invoking a name with no
active restart is an error. Because handlers run before unwinding, a
handler can inspect the condition and choose a restart established
*below* it, resuming computation there. This is the retry / use-value /
abort mechanism.

Restarts are identified by name only; two nested `restart-case`s may
reuse a name, the innermost winning.

### 8.5 Restart introspection

Active restarts are first-class values of type `restart` (printed
`#<restart name>`, compared by identity). This is the primitive a host
needs to present recovery choices as a prompt.

- `(compute-restarts)` → array of the currently active restarts,
  innermost first.
- `(find-restart name)` → the innermost active restart with that name
  (symbol/keyword/string), or `nil`.
- `(restart-name r)` → the restart's name as a symbol.
- `(restart-description r)` → its description string, or `nil`.

`invoke-restart` accepts a restart value in place of a name and unwinds
to exactly that restart. A restart value whose `restart-case` has
already exited is no longer active; invoking it is an error.

Deliberate omissions: there is no non-unwinding `restart-bind`, and no
`warn`/`cerror` tier -- restarts always transfer control, and anything
softer is a library's job.

### 8.6 `try`

```
(try body…
  (catch (pred var) forms…)
  …)
```

Unwinding sugar for the common case. Runs the body; if an unwinding
condition (not quit) reaches the `try`, each `catch` clause's `pred` is
applied to the condition in order. The first truthy match binds the
condition to `var` and evaluates its forms as the result. No match means
the condition keeps unwinding. The catch clauses are *not* active as
signal-site handlers; `try` only intercepts conditions that are already
unwinding. `catch` clauses are recognized as trailing forms whose head is
the symbol `catch`; body forms must precede them.

### 8.7 `unwind-protect`

```
(unwind-protect expr cleanup…)
```

Evaluates `expr`, then evaluates the cleanup forms in order regardless of
whether `expr` returned, raised, invoked a restart, or was quit. Returns
`expr`'s value; a cleanup form that itself raises replaces the in-flight
result. `defer` is an alias.

---

## 9. Dynamic params

A **param** is a dynamically-scoped cell: a rendezvous point that lets a
caller and a callee coordinate through a well-known name without
threading an argument between them.

```
(defparam name ["docstring"] default)
```

`defparam` is the only way to make one. Like `defmacro`, it defines
`name` as a var in the current namespace, holding a fresh param whose
default is `default`. It is a top-level form; a `defparam` inside a
function or `let` body is an error. Params are always named,
namespace-level things -- there is no anonymous constructor.

`(p)` reads a param's current value: the innermost active `with-params`
binding, else the default (3.3).

```
(with-params ((param expr) …) body…)
```

Each `param` expression must evaluate to a param. Binding is
**sequential**, like `let`: each binding is installed before the next
pair's expressions evaluate, so later `expr`s (and later `param`
expressions) see earlier bindings in effect. The body runs with all
bindings installed, and they are removed on every exit path (normal,
raise, restart, quit).

---

## 10. Core library

All bindings live in `otium.core` and are referred into every namespace.
Notation: `[x]` optional, `…` variadic, `→` result. "Sequence" means a
proper list, an array, or `nil` (treated as empty). Functions documented
as *kind-preserving* return a list when given a list or `nil`, an array
when given an array.

### 10.1 Arithmetic

Int/int operations yield ints; overflow wraps two's-complement at the
implementation's int width (Appendix A). Any float operand contaminates
to float.

| form | behavior |
|---|---|
| `(+ n…)` → num | Sum; `(+)` is `0`. |
| `(* n…)` → num | Product; `(*)` is `1`. |
| `(- n n…)` → num | Left fold; `(- n)` negates. |
| `(/ n n…)` → num | Division; `(/ n)` is `1/n`. Int÷int stays int when exact (`(/ 6 2)` is `3`), else float (`(/ 7 2)` is `3.5`). Int division by zero is an error. |
| `(quotient a b)` → int | Truncating int division. Ints only; zero divisor errors. |
| `(remainder a b)` → int | Remainder, sign of the dividend. |
| `(modulo a b)` → int | Modulus, sign of the divisor. |
| `(abs n)` → num | Absolute value. |
| `(min n n…)` / `(max n n…)` → num | Extremum (numeric comparison). |
| `(floor n)` `(ceiling n)` `(round n)` → int | Round a float to int (identity on ints); result outside int range errors. `round` rounds half away from zero. |
| `(= n n…)` → bool | Numeric equality chain across int/float. |
| `(< …)` `(> …)` `(<= …)` `(>= …)` → bool | Comparison chains; at least two arguments. |
| `(inc n)` / `(dec n)` → num | `n±1`. |
| `(zero? n)` `(pos? n)` `(neg? n)` → bool | Sign tests (numeric). |
| `(even? n)` / `(odd? n)` → bool | Parity (ints). |

### 10.2 Predicates

`(not x)` → `#t` iff `x` is falsy.

Type predicates, each `(p x)` → bool: `nil?`, `null?` (empty list),
`boolean?`, `int?`, `float?`, `number?` (int or float), `symbol?`,
`keyword?`, `string?`, `pair?`, `array?`, `table?`, `buffer?`, `macro?`,
`procedure?` (applicable function), `list?` (proper list: `()` or a
nil-terminated pair chain).

`(true? x)` / `(false? x)` -- `#t` only for the booleans themselves.

`(empty? coll)` → bool -- no elements; accepts nil, `()`, pair, array,
table, string, buffer; other types error.

`(eq? a b)`, `(equal? a b)` -- see 2.4.

`(type x)` → keyword -- the type name of section 2.

### 10.3 Lists and pairs

| form | behavior |
|---|---|
| `(cons a d)` → pair | |
| `(car p)` / `(cdr p)` | Head/tail of a **pair**; strict, `(car '())` is an error. |
| `(caar p)` `(cadr p)` `(cddr p)` | Compositions. |
| `(list x…)` → list | |
| `(append list…)` → list | Concatenation; arguments must be proper lists. |
| `(length coll)` → int | Elements of a list, array, table (entries), string or buffer (characters). Improper lists and other types error. |
| `(reverse seq)` → seq | Kind-preserving. |
| `(list->array list)` → array, `(array->list array)` → list | Conversions, strict about input type. |

### 10.4 Sequences

These accept any sequence (list / array / nil) and never error on
emptiness; misses yield `nil`.

| form | behavior |
|---|---|
| `(first seq)` | First element or `nil`. On a pair, the car; on a string, the first character as a string or `nil`. |
| `(rest seq)` | All but the first; kind-preserving; empty/`nil` is `()`. On a pair, the cdr. |
| `(second seq)` / `(third seq)` | `(nth seq 1)` / `(nth seq 2)`. |
| `(last seq)` | Last element or `nil`. |
| `(nth seq i)` | Element at 0-based `i`, or `nil` out of range. |
| `(count coll)` → int | Like `length` but `nil` counts as 0; accepts sequences, tables, strings. |
| `(map f seq)` | Kind-preserving; applies `f` to each element in order. |
| `(filter pred seq)` | Kind-preserving; keeps elements where `pred` is truthy. |
| `(reduce f init seq)` | Left fold: `(f acc elem)`. |
| `(for-each f seq)` → nil | `f` for side effects. |
| `(range n)` `(range a b)` `(range a b step)` → array | Ints from `a` (default 0) toward `b`, exclusive, by `step` (default 1, nonzero; may be negative). |
| `(concat seq…)` | Concatenation; kind follows the **first** argument. |
| `(take n seq)` / `(drop n seq)` | Kind-preserving prefix/suffix; `n` clamps. |
| `(contains? coll x)` → bool | Key presence in a table, element (`equal?`) in a sequence, substring in a string; `nil` is `#f`. |
| `(sort seq)` | Ascending, kind-preserving; orderable classes per 2.5. |
| `(sort-by keyfn seq)` | Sort by derived keys (same class rules); stable. |
| `(group-by keyfn seq)` → table | Key → array of elements, in first-seen key order. |
| `(frequencies seq)` → table | Element → count. |

### 10.5 Tables and arrays

| form | behavior |
|---|---|
| `(array x…)` → array | The `[...]` constructor. |
| `(table k v …)` → table | The `{...}` constructor; odd argument count errors; `nil` values delete (2.2). |
| `(get coll k [default])` | Nil-tolerant lookup: table by key; array or string by int index (other key types miss); `nil` coll is a miss. Misses yield `default` or `nil`. Other types error. |
| `(get-in coll path [default])` | `get` folded over a sequence of keys. |
| `(put! coll k v …)` → coll | Mutates in place. Tables: insert, or delete on `nil` value. Arrays: `k` must be an in-range int index (no growth). Multiple k/v pairs allowed. |
| `(push! arr x…)` → arr | Append to an array. |
| `(pop! arr)` | Remove and return the last element, or `nil` when empty. |
| `(update! coll k f args…)` → coll | `(put! coll k (f (get coll k) args…))`. |
| `(assoc coll kvs…)` → copy | `put!` on a shallow copy; `(assoc nil …)` builds a fresh table. |
| `(dissoc coll k…)` → copy | Copy without the keys. |
| `(update coll k f args…)` → copy | `assoc` of `(f (get coll k) args…)`. |
| `(keys t)` / `(values t)` → array | Insertion order; `nil` gives `[]`. |
| `(merge t…)` → table | New table, left to right; `nil` arguments skipped; `nil` values delete. |
| `(copy coll)` | Shallow copy of an array or table; `(copy nil)` is `nil`. |

### 10.6 Strings, buffers, names

All positions are in characters (code points).

| form | behavior |
|---|---|
| `(str x…)` → string | Concatenated display forms; `(str)` is `""`. |
| `(string-append s…)` → string | Strict: strings only. |
| `(string-length s)` → int | |
| `(substring s start [end])` → string | Indices clamped to `[0, length]`; `end` < `start` yields `""`. |
| `(string-split s [sep])` → array | On the literal separator (non-empty), or on whitespace runs with none. |
| `(string-join sep seq)` → string | Joins display forms. |
| `(string-upcase s)` / `(string-downcase s)` → string | |
| `(string-trim s)` → string | Strip surrounding whitespace. |
| `(string-contains? s sub)` `(string-starts-with? s p)` `(string-ends-with? s p)` → bool | Literal tests. |
| `(string-replace s from to)` → string | Every literal occurrence. |
| `(string->number s)` | Int or float per 1.3 (surrounding whitespace ok), else `nil`. |
| `(number->string n)` → string | Display form. |
| `(string->symbol s)` / `(symbol->string sym)` | |
| `(symbol x)` / `(keyword x)` | Coerce a string/symbol/keyword. |
| `(name x)` → string | The name of a symbol, keyword, or string. |
| `(buffer [x])` → buffer | Mutable string builder, optionally seeded with `x`'s display form. |
| `(buffer-push! b x…)` → b | Append display forms. |
| `(buffer->string b)` → string | Freeze; the buffer remains usable. |

### 10.7 Output

Output goes to the host-defined output sink.

| form | behavior |
|---|---|
| `(display x…)` / `(print x…)` → nil | Display forms, space-separated, no newline. |
| `(write x…)` → nil | Repr forms, space-separated, no newline. |
| `(println x…)` → nil | `display` plus a newline. |
| `(newline)` → nil | |

### 10.8 Functions and evaluation

| form | behavior |
|---|---|
| `(apply f a… seq)` | Call `f` with leading args followed by the sequence's elements. |
| `(identity x)` | |
| `(partial f a…)` → fn | Prepends `a…` to the call's arguments. |
| `(comp f…)` → fn | Right-to-left composition; `(comp)` is `identity`. |
| `(eval form)` | Macroexpand and evaluate a value as code in the current namespace (no lexical scope). |
| `(read-string s)` → value | Read exactly one form; empty or trailing input errors. |
| `(macroexpand-1 form)` → value | If `form` is a macro call (per the head rule of section 6), call the macro once and return the result; otherwise return `form` unchanged. |
| `(macroexpand form)` → value | `macroexpand-1` repeated until the head is no longer a macro call. Neither function descends into subforms. |
| `(gensym [prefix])` → symbol | Section 6. |

### 10.9 Conditions

`error`, `signal`, `invoke-restart` -- section 8; `define-condition`,
`condition-of-type?` -- 8.1; `compute-restarts`, `find-restart`,
`restart-name`, `restart-description` -- 8.5. Helpers over the
condition convention: `(condition-type c)` is the `:type` of a condition
table else `nil`; `(condition-message c)` is `:message` or `nil`;
`(error? c)` tests for a table with a `:type`; `(type-pred t)` returns a
predicate matching conditions whose type is `t` or a registered subtype
of it (8.1).

### 10.10 Namespaces and reflection

`in-ns` (7.2); `(current-ns)` is the current namespace's name as a
symbol; `(describe sym)` is `"ns/name: docstring"` for the var `sym`
resolves to (or a no-documentation placeholder), `nil` if unresolvable.
Params are made and bound by the special forms `defparam` and
`with-params` (section 9), not by library functions.

### 10.11 Macros (prelude)

| form | behavior |
|---|---|
| `(when test body…)` | `body` as `begin` when truthy, else `nil`. |
| `(unless test body…)` | Inverse. |
| `(defn name params body…)` | `(define (name . params) body…)`. |
| `(-> x forms…)` | Thread-first: insert `x` as the first argument of each form (bare symbols call as `(form x)`). |
| `(->> x forms…)` | Thread-last. |
| `(dotimes (i n) body…)` | `i` from 0 below `n`. |

---

## Appendix A: implementation limits and latitude

- **Int width** is implementation-defined, with at least 31 bits of
  signed range. Arithmetic overflow wraps two's-complement at that
  width -- never undefined behavior. Programs that want to run on every
  implementation keep magnitudes within 31 bits; the conformance suite
  does. Float→int conversion (`floor`/`ceiling`/`round`) outside the
  int range remains an error, as does reading an out-of-range literal.
- **Non-tail recursion depth** may be bounded. The bound must be at least
  a few thousand frames, and exceeding it must be a catchable error, not
  a crash.
- **Interrupt granularity** (3.6) is implementation-defined but must be
  bounded.
- **Table iteration order** is insertion order everywhere (printing,
  `keys`, `values`, `merge`, `group-by`, …).
- Unicode case mapping and whitespace classification may be
  ASCII-conservative, but lengths, indexing, and `substring` must be
  code-point correct.
- Float printing: shortest round-trip form, with integral floats keeping
  a trailing `.0`.
- The host embedding API, the REPL, and the load-path contents are
  outside this specification.
