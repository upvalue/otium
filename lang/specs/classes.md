# Classes and generic functions

Implemented on the C and Chez runtimes. Classes replace typed maps with fixed
slots and a single chain of slotless abstract types. Generic functions support
ancestor dispatch, `call-next-method`, and `:around` methods.

## Why

Planning the UI library kept producing requests for typed maps:

| UI need | What we were about to add |
| --- | --- |
| Boids reads ~1.4M fields per frame | Fixed field layouts and inline caches |
| `(g/field-ref b 'x)` everywhere | Callable maps, `(b :x)` |
| Arithmetic-heavy code | `with-fields`, binding fields to local names |
| Widget defaults and typo checks | `define-widget` with defaults and known keys |
| Generic calls on every widget | A dispatch cache |
| Editing a type while the program runs | Migrating existing objects to a new layout |

Each of those is part of a class system. Rather than grow typed maps one
feature at a time, this designs the whole thing once.

The UI also wants defaults that apply to a family of types: a `paint` for any
widget, a `measure` for anything that shows text. Today the only default a
generic can have is one unspecialized catch-all.

Inheritance is deliberately limited. Data is never inherited: a class can't be
subclassed, and code reuse between classes goes through slots holding other
objects. Abstract types exist only so methods can apply to a family of classes.
Each type has at most one parent, so there is no multiple inheritance and no
linearization algorithm.

## Prior art

- [Julia](https://docs.julialang.org/en/v1/manual/types/), the model. Concrete
  types are final, abstract types form a single-parent tree, and ambiguous
  methods are errors.
- [CLOS](https://www.lispworks.com/documentation/HyperSpec/Body/07_.htm), for
  `call-next-method`, `:around` methods, and updating instances when a class is
  redefined.
- [Dylan](https://opendylan.org/books/drm/), for sealing as a later performance
  step.
- gpui and egui, Rust UI libraries with no inheritance at all. Shared behaviour
  like focus is an object a widget holds, not something it inherits.

## Naming

Declaration forms use the short `def` prefix: `defclass`, `defabstract`,
`defgeneric`, `defmethod`. The last two replace `define-generic` and
`define-method`.

`defabstract` follows Julia's `abstract type`, the closest precedent: no
fields, no instances, one parent. In Java, C# and Dylan an abstract class can
hold fields its subclasses inherit; here it can't.

## At a glance

```scheme
(defabstract Widget)

(defclass Button :is Widget
  (key :init nil)
  (label :init "")
  (on-press :init nil))

(defclass Label :is Widget
  (key :init nil)
  (text :init ""))

(def b (make Button :label "7" :on-press (fn () (press! "7"))))
(b :label)                  ; => "7"
(slot-set! b :label "8")

(defgeneric (paint widget g bounds))

;; Default for every widget: draw nothing.
(defmethod (paint (w Widget) g bounds) nil)

(defmethod (paint (w Button) g bounds)
  (draw-box g bounds)
  (draw-text g bounds (w :label)))

;; Debug outlines around one kind of widget, without touching its paint method.
(defmethod :around (paint (w Button) g bounds)
  (call-next-method)
  (when debug? (draw-outline g bounds)))
```

## Classes

`(defclass Name slot ...)` or `(defclass Name :is Abstract slot ...)`
declares a class in the current namespace. Each slot is a symbol or
`(symbol option ...)`.

Classes are final. Nothing can name a class after `:is`, so slots are never
inherited. Without `:is`, the parent is `Object`.

Slot options:

| Option | Meaning |
| --- | --- |
| `:init expr` | Default value. `expr` is evaluated for each `make` that doesn't supply the slot, so `(items :init [])` gives each instance its own array. |
| `:accessor name` | Also define `name` as a generic function reading the slot. Optional; see below. |
| `:state boolean` | Mark library-owned state, default `#f`. A state slot requires `:init`; property construction skips it and mounting initializes it. |

A slot with no `:init` must be supplied to `make`. This catches forgetting a
field. Optional slots say so with `:init nil`.

Slots are declared as symbols and accessed with keywords, as in Clojure records.
`(make Button :label "7")` passes slot values as alternating keywords and values.
An unknown keyword is an error. There are no separate initarg names.

Instances have a fixed set of slots. There is no adding or deleting slots on
one instance. Plain tables remain the type for open-ended data, and a class can
have a slot holding one.

Instances are not tables. `table?` is false, and `get` and `put!` don't apply.
That is a change from typed maps, which are tables on C.

Equality is identity. `copy` makes a shallow copy with the same class. Instances
print as `#<Button>` and are not readable.

### Reading and writing slots

`(b :label)` reads a slot. An instance applied to a keyword returns that slot,
or raises an error naming the class and slot if it has none. The keyword can
be computed, `(b k)`, so no separate `slot-ref` is needed.

`(set! (b :label) "8")` writes one and returns the value. `slot-set!` is the
same operation as a function, for passing to higher-order code.

`:accessor` is there for classes whose slots are a public API and might later
become computed. Making it automatic for every slot was rejected: accessors
would be namespace bindings, and ordinary local names like `x` would shadow
them. `(let ((x (x b))) ...)` is exactly what boids code wants to write.

### Assigning through a call

`set!` accepts a two-element target as well as a name:

```scheme
(set! (b :x) v)     ; => (set-applied! b :x v)
(set! (arr 3) v)
(set! (tbl :k) v)
```

The expander rewrites it to `set-applied!`, a built-in function. The expander
can't tell whether `b` is an instance, a table or something else, so
`set-applied!` decides at run time:

| Target | Effect |
| --- | --- |
| Instance | Write the slot, as `slot-set!` |
| Table | `put!` |
| Array | Set the element at that index |
| Anything else | Error naming the type |

Anything that can be read by calling it with one argument can be assigned the
same way. The object, key and value are evaluated in that order. Slot writes
through `set-applied!` get an inline cache like slot reads.

This is not Common Lisp's `setf`. There is no registry of setters, so
`(set! (car p) v)` or `(set! (my-accessor obj) v)` are errors. That can grow
later if needed.

The shared expander lowers applied assignment to `set-applied!` on both
runtimes. In `with-slots`, assignment to a plain name goes through a local
symbol macro.

### `with-slots`

```scheme
(defn move! (b w h)
  (with-slots b (x y vx vy)
    (set! x (wrap (+ x vx) w))
    (set! y (wrap (+ y vy) h))))
```

Inside the body each name reads the slot when used, and `set!` on the name
writes it. The shared expander implements these names as local symbol macros.
The object expression is evaluated once. Inner lexical bindings shadow the
slot names normally.

## Abstract types

`(defabstract Name)` or `(defabstract Name :is Parent)` declares an
abstract type. It has no slots and no instances; `make` rejects it. Its only
use is as a method specializer and as a parent for classes and other abstract
types.

Every type has at most one parent, and the chain ends at `Object`. So each
class has exactly one line of ancestors:

```
Button -> Widget -> Object
```

Sharing slots between classes is done by composition. A class that needs
another object's state and behaviour holds it in a slot and forwards calls to
it explicitly:

```scheme
(defclass Scrollbar
  (offset :init 0)
  (extent :init 0))

(defclass ListView :is Widget
  (items :init [])
  (scroll :init (make Scrollbar)))

(defmethod (handle-event (v ListView) (e Wheel))
  (handle-event (v :scroll) e))
```

Forwarding stays explicit. A `:delegate` slot option that generates forwarding
methods is deferred until real code shows it is repetitive.

## Built-in classes

The built-in dispatch identities (`Number`,
`String`, `Keyword`, `Hashtable`, `Procedure`, ...) become built-in classes
with `Object` as their parent. They are final like any other class. Abstract
parents for them, such as `Sequence` over lists and arrays or `Real` over
integers and floats, are deferred. Adding them later doesn't break existing
methods.

A method specialized on `Object` and one that leaves the parameter
unspecialized mean the same thing.

## Generic functions and method selection

`defgeneric` and `defmethod` use fixed arity and the syntax of the former
`define-generic` and `define-method`, renamed to follow the `def` prefix.

A method is applicable when, for each specialized argument, the specializer is
the argument's class or one of its ancestors.

For one argument position, a specializer closer to the argument's class in its
chain is more specific. Method A is more specific than B if it is at least as
specific in every position and strictly more specific in at least one.

If one applicable method is more specific than all the others, it runs. If not,
the call is ambiguous and raises an error listing the candidates, as it does
today. With single chains, ambiguity can only come from conflicts across
arguments, such as `(Button, Event)` against `(Widget, PointerDown)`. One
argument position always has a clear order.

## `call-next-method` and `:around`

Two kinds of method:

- **Primary** methods (no qualifier). The most specific runs. It can call
  `(call-next-method)` to run the next most specific, with the same arguments,
  or with new ones as `(call-next-method a b)`. `(next-method?)` checks whether
  there is one.
- **`:around`** methods wrap the primary methods, most specific outermost.
  Each decides whether and when to `(call-next-method)`. The innermost one's
  next method is the most specific primary.

The result is the outermost method's result.

Replacement arguments reselect from the remaining methods that were applicable
to the original call. They must have the generic's fixed arity. They do not
restart dispatch or introduce methods excluded by the original arguments.
`next-method?` reports whether a remaining step exists, including an ambiguous
step that will fail if called. Outside a method it returns false; calling
`call-next-method` there raises an error.

"Next most specific" is well defined only when the remaining methods have a
single most specific one. With two arguments that can fail below the top: a
`(Button, PointerDown)` method's next candidates might be both
`(Widget, PointerDown)` and `(Button, Event)`, neither more specific than the
other. `call-next-method` into such a tie is an ambiguity error naming the
candidates.

A method's identity is its qualifier plus its signature. Redefining the same
qualifier and signature replaces it.

CLOS also has `:before` and `:after` methods. They are left out here, since
the wrapping a UI needs (clipping, skipping disabled widgets) belongs to the
framework's own code that calls `paint` and `handle-event`. `:around` covers
the remaining cases.

## Property descriptions and state

A slot can carry `:state #t` metadata and must then have an `:init` expression.
`:state #f` is the default. Ordinary `make`, reads, writes and accessors treat
state slots like any other slot. The metadata supports the widget lifecycle
in `specs/ui.md`; state still uses fixed slots and cached access.

- `(class-properties Class keyword value ...)` constructs a property instance,
  rejecting state arguments and leaving state slots as `nil` without running
  their initializers. Property defaults run normally.
- `(class-mount properties)` copies properties to a new instance and evaluates
  its state initializers once, in declaration order.
- `(class-update-properties! instance properties)` copies properties into an
  existing instance of the same class, retaining state. When the class layout
  changed, it supplies new properties during migration, before `update-instance`
  runs, so newly required properties can come from the caller.
- `(class-subtype? child parent)` tests identity or ancestry between classes.

Mounting and updating reject property instances with an outdated layout. Build
a fresh description after class redefinition. These are construction primitives
for libraries; application widget methods operate on fully mounted instances.

## Redefinition and reload

Re-evaluating `defclass` or `defabstract` keeps the type's identity,
including the identity of imported aliases. Methods specialized on it keep working, and so do
existing instances.

When a class's slots change:

- Each instance points to a layout. The class points to its current layout.
- Redefinition creates a new layout. Existing instances keep the old one until
  something reads or writes them.
- On that access, the instance is migrated: added slots get their `:init`
  value, removed slots are dropped, kept slots keep their values unless their `:state` flag changed. A changed
  flag resets the slot to its initializer, or to the supplied property during
  `class-update-properties!`.
- The generic `(update-instance obj old-slots)` runs after migration. Its
  default does nothing. Specialize it to convert data, such as splitting a
  `name` slot into `first` and `last`. This is a reduced version of CLOS's
  `update-instance-for-redefined-class`.

A new slot with no `:init` can't be filled in for existing instances.
Migrating such an instance is an error unless `update-instance` sets the slot.

`old-slots` is a table keyed by slot keywords. The hook sees the new layout and
can write required slots. If initialization or the hook fails, the instance's
layout and slot values are restored; a later access retries migration. Side
effects outside the instance, including mutations of shared slot contents, are
not rolled back.

Changing a type's parent clears dispatch caches. Instances need no migration,
because parents carry no slots.

The check that triggers migration is the same layout comparison inline caches
already perform, so reload support costs nothing extra on normal access.

`(reset-class! Name slot ...)` explicitly creates a new class identity, accepting
the same parent and slot syntax as `defclass`. Existing instances and methods
retain the old identity. `(reset-generic! (name arguments ...))` replaces a
generic with a fresh one and no methods.

`(remove-method! generic signature)` removes a primary method. An optional third
argument, `:around` or `'primary`, selects the qualifier. The result says whether
a method was removed. Signatures are lists of class descriptors; `Object` and
`#f` both mean an unspecialized argument.

`(generic-methods generic)` returns detached `(qualifier signature)` lists in
unspecified order. Qualifiers are the symbol `primary` or the keyword `:around`.
Signatures use `Object` for unspecialized arguments.

## Performance

- **Instances are a slot vector** plus a layout pointer. A four-slot `Boid` is
  one object of seven words on C: the header, layout pointer, stable identity
  hash, and four values. Chez uses an opaque record with a slot vector.
- **Inline caches at slot reads.** `(b :x)` with a literal keyword remembers the
  last layout seen and its slot index. A hit is a pointer comparison and an
  index. A miss looks the slot up in the layout and updates the cache.
- **`with-slots`** expands to cached slot accesses at each use.
- **A dispatch cache per generic.** It maps a tuple of argument classes to the
  effective method, built once as a chain of closures. It's cleared when a
  method is added or removed or a type is redefined. The first call with a new
  combination of classes pays for selection.
- **Single dispatch fast path.** Most UI generics specialize one argument.
  Caching on that argument's class alone avoids building a tuple key.

Single chains also make selection cheap on a cache miss: each argument's
ancestors are a short list, with no linearization to compute.

Dylan's sealing, letting a library declare that a generic won't gain methods
so calls can be resolved ahead of time, is a possible later step.

## Left out

- Inheriting slots, and multiple parents of any kind.
- A meta-object protocol.
- `:before` and `:after` methods, and custom method combination.
- Value specializers (CLOS `eql` specializers).
- `change-class`, class-allocated (shared) slots, and slot type declarations.
- Optional and rest arguments in generics.
- Serialization of instances.

## Relation to the multimethods work

This keeps:

- Generic functions and methods, fixed arity, and replacement by signature.
  `define-generic` and `define-method` are renamed `defgeneric` and
  `defmethod`.
- Declaration identity across reload, namespace ownership, imports and aliases.
- The built-in dispatch identities, now classes.
- Ambiguity errors.
- The error codes, with new ones for class definition and slot access.

This replaces:

- `define-type`, typed maps, and the `field-*` operations, with `defclass`,
  instances and slots.
- Exact-match selection, with selection along each argument's chain.

It adds abstract types, `call-next-method`, `:around` methods, and instance
migration.

The typed-map constructors and `field-*` operations have been removed from
both runtimes. The multimethod examples and tests use classes.

## Validation

`make test-classes` runs the same class checks on C, C with collection stress,
and Chez. Runtime-specific suites cover namespace ownership, imported method
extensions, file reloads, explicit resets, and dispatch against an independent
oracle. `make test` runs these with the other runtime and shared tests.

`benchmarks/workloads/classes.ot` exercises slot reads and writes, ancestor
dispatch, and an around method using the same source on both runtimes.

## Decisions

- Classes replace typed maps entirely. Plain tables remain for open data.
- No slot inheritance and no multiple parents. Abstract types form single
  chains, as in Julia.
- Ambiguity across arguments stays an error.
- `call-next-method` and `:around` methods; no `:before` or `:after`.
- `def`-prefixed declaration names, including renaming the generic forms.
- Slots without `:init` are required.
- `set!` on a one-argument call target, through `set-applied!`.
- Composition uses explicit forwarding.

## Deferred

- A `:delegate` slot option.
- Abstract parents for built-in classes.
- Observable slot writes, for UI change tracking. The UI starts by re-running
  from the root after each event, which doesn't need them. If they come, they
  would be a per-class option, since making every write a generic call
  conflicts with the performance goals.
