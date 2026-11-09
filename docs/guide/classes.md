# ✨ Records and classes

Use a table for loose data, such as room descriptions, game settings, or
anything whose keys vary from value to value. A record fits values that should
all have the same fields, with errors for misspelt field names. Choose a class
when behaviour should depend on the kind of value, such as using a sword
differently from a potion.

Records hold fields. Class instances hold slots, and generic functions choose
methods based on their arguments' types. Methods belong to the generic function,
not to an individual class.

The examples build on each other in one session.

## Records

A room's coordinates have a fixed shape. Declare a `Position` record and
construct it with values in field order:

```otium
(defrecord Position (x y))
(def entrance (record-new Position 2 3))

(entrance :x)                    ; => 2
```

`record-new` requires every field; missing or extra values are errors. Access
fields using keywords. An unknown field is also an error.

Fields are mutable by default. You can read and assign through a call, or use
the explicit record operations:

```otium
(set! (entrance :x) 4)
(record-ref entrance :x)         ; => 4

(record-set! entrance :y 5)
(entrance :y)                    ; => 5
```

Use `:immutable #t` when fields should not be reassigned:

```otium
(defrecord Exit (direction destination) :immutable #t)
(def north-exit (record-new Exit :north "Hall"))

(north-exit :destination)        ; => "Hall"
```

Both assignment forms reject writes to an immutable record. Immutability is
shallow: a field can still contain a mutable value.

Records have no methods or inheritance. Their field lists and mutability cannot
change while the program runs. You can re-evaluate an unchanged declaration, but
changing it requires a new record name or restarting the program.

## Classes and slots

A class declares the slots its instances hold. A room can have a required name
and position, with a default visitation flag:

```otium
(defclass Room
  name
  position
  (visited :init #f))

(def hall (make Room :name "Hall" :position entrance))

(hall :visited)                 ; => #f
```

`make` takes keyword/value pairs. A slot without `:init` is required, so
omitting `name` or `position` is an error. For each new instance, each omitted
slot's default is evaluated separately.

Read and write slots with the same call syntax used for record fields:

```otium
(set! (hall :visited) #t)
(hall :name)                    ; => "Hall"
((hall :position) :x)           ; => 4
```

Unknown slot names are errors. An instance is not a table: use slot access
rather than `get` or `put!`. You cannot add a slot to just one instance, though
a declared slot can hold a table for open-ended data.

## Abstract types group classes

An abstract type names a family of types for method dispatch. It has no slots
and cannot be instantiated. Use `:is` to place a class in that family:

```otium
(defabstract Item)
(defabstract Creature)

(defclass Sword :is Item
  (cursed :init #f))

(defclass Potion :is Item
  (cursed :init #f))

(defclass Troll :is Creature
  (wounds :init 3))

(defclass Bat :is Creature)
```

Classes are final. Unlike CLOS or Java, Otium does not let one class extend
another class. A class can extend only an abstract type. Abstract types can
themselves extend an abstract type, and each type has at most one parent.

Methods specialized on the abstract type provide shared behaviour. Slots are
never inherited: `Sword` and `Potion` each declare `cursed`. To share a larger
piece of state, hold another object in a slot, as `Room` holds a `Position`.

Create the game's items and creatures:

```otium
(def sword (make Sword))
(def potion (make Potion))
(def troll (make Troll))
(def bat (make Bat))

(is-a? sword Item)               ; => #t
```

## Generic functions choose behaviour

A generic function names an operation and declares its fixed argument list.
Methods supply implementations. In a method parameter such as `(thing Sword)`,
`Sword` is the type that parameter must match.

```otium
(defgeneric (describe-thing thing))

(defmethod (describe-thing (thing Item))
  "An item lies here.")

(defmethod (describe-thing (thing Creature))
  "A creature watches you.")

(defmethod (describe-thing (thing Sword))
  "A steel sword lies here.")

(describe-thing sword)           ; => "A steel sword lies here."
(describe-thing potion)          ; => "An item lies here."
(describe-thing troll)           ; => "A creature watches you."
(describe-thing bat)             ; => "A creature watches you."
```

An abstract-type method applies to every class in its family. The `Sword` method
wins over the `Item` method because it is more specific. An unspecialized
parameter, written as just a name, accepts any value. Built-in values also have
dispatch classes, such as `Number` and `String`.

### Dispatch on both arguments

Using an item depends on both the item and its target. Specialize both
parameters:

```otium
(defgeneric (use item target))

(defmethod (use (item Item) (target Creature))
  "Nothing happens.")

(defmethod (use (item Sword) (target Troll))
  "The troll retreats.")

(defmethod (use (item Potion) (target Bat))
  "The bat falls asleep.")

(use sword troll)               ; => "The troll retreats."
(use potion bat)                ; => "The bat falls asleep."
(use sword bat)                  ; => "Nothing happens."
```

A method wins only if it is at least as specific in every argument position and
more specific in at least one. If two applicable methods are tied, with neither
more specific overall, the call raises an ambiguity error.

For example, methods for `(Sword, Creature)` and `(Item, Troll)` would conflict
on a sword used on a troll. A method for `(Sword, Troll)` resolves that
conflict. Definition order does not break ties.

## Calling the next method

A specialized method can build on a more general one. Put `:next` followed by a
local name immediately after the method signature. Call that name with all
arguments explicitly:

```otium
(defmethod (describe-thing (thing Sword)) :next next
  (string-append (next thing) " Its blade is steel."))

(describe-thing sword)
; => "An item lies here. Its blade is steel."
```

This replaces the earlier `Sword` method. `next` calls the less-specific `Item`
method, not the replaced definition. You can choose the local name; its value is
`#f` when no next method exists.

Redefining a method with the same signature and qualifier replaces it.
Subsequent generic calls use the new method, so behaviour can change without
recreating the game's objects.

### Around methods

An `:around` method wraps the primary methods. It can call the next method and
alter its result, or return without calling it.

Here, cursed items misfire before any primary `use` method runs:

```otium
(defmethod :around (use (item Item) (target Creature)) :next next
  (if (item :cursed)
      "The curse turns the item against you."
      (next item target)))

(use sword troll)               ; => "The troll retreats."

(set! (sword :cursed) #t)
(use sword troll)
; => "The curse turns the item against you."
```

The primary methods remain unchanged. If several around methods apply, the most
specific wraps the others. Calling `next` eventually reaches the primary
methods.

## Changing classes while the game runs

You can change class declarations without replacing existing instances.
Re-evaluate the full declaration with a new slot:

```otium
(defclass Bat :is Creature
  (health :init 10))

(bat :health)                   ; => 10
(describe-thing bat)             ; => "A creature watches you."
```

The existing `bat` picks up `health` when it is accessed. Existing slots keep
their values, added slots receive their defaults, and removed slots disappear.
The class keeps its identity, so methods specialized on it still apply.

### Converting old data

When defaults are not enough, specialize `update-instance`. It receives the
instance with its new slots and a table of its old slot values, keyed by
keywords.

The troll currently tracks wounds. Replace that with remaining health, using the
old wounds to calculate the new value. Install the conversion method before
accessing instances under the new declaration:

```otium
(defmethod (update-instance (creature Troll) old-slots)
  (set! (creature :health)
        (- 20 (get old-slots :wounds 0))))

(defclass Troll :is Creature
  (health :init 20))

(troll :health)                 ; => 17
((make Troll) :health)          ; => 20
```

`update-instance` converts existing instances; ordinary `make` uses the new
declaration's defaults. If you add a required slot without `:init`, the
conversion method must fill it for existing instances.

See [Live programming](live-programming.md) for keeping game state across
reloads.

<!-- generated from classes.md.spec sha256:9da0c1b249a31036d8c4531eb86969c62f96d68c7850739f48671750fc7bede0 -->
