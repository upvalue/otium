# ✨ Records and classes

Use records for plain data with fixed fields. Use classes when values need
shared behavior through generic functions. The examples below build a small
model of animals.

## Store plain data in records

Declare a record's fields, then supply their values in order:

```scheme
(defrecord AnimalTag (name number))

(def tag (record-new AnimalTag "Milo" 17))

(tag :name)                    ; => "Milo"
(record-ref tag :number)        ; => 17
(set! (tag :number) 18)          ; => 18
```

`record-new` requires exactly one value per field. Fields are named with symbols
in the declaration and keywords when accessing them. Unknown fields are errors.

`defrecord` also defines named helpers:

```scheme
(def spare-tag (make-animal-tag :name "Pip"))

(animal-tag? spare-tag)                 ; => #t
(animal-tag-number spare-tag)           ; => nil
(set-animal-tag-number! spare-tag 19)    ; => 19
```

The generated keyword constructor fills omitted fields with `nil`.

Records are mutable by default. To prevent field assignment, declare an
immutable record:

```scheme
(defrecord AnimalBirth (year month) :immutable #t)

(def birth (record-new AnimalBirth 2024 6))
(birth :year)                  ; => 2024
```

Immutability is shallow: a field can still hold a mutable array or table.
Records compare by identity, even when immutable. They have no inheritance and
are separate from the class system.

## Define classes and parents

An abstract type groups classes for method dispatch. Define `Animal`, then two
concrete classes:

```scheme
(defabstract Animal)

(defclass Dog :is Animal
  name
  (age :init 0)
  (tricks :init []))

(defclass Cat :is Animal
  name
  (age :init 0))

(def dog (make Dog :name "Milo"))
(def cat (make Cat :name "Pip" :age 3))

(dog :name)                    ; => "Milo"
(dog :age)                     ; => 0
```

Class fields are called *slots*. A bare slot such as `name` is required. A slot
with `:init` gets its default only when omitted from `make`. Each construction
evaluates its own defaults, so each dog gets a separate `tricks` array. Use
`:init nil` for an optional slot with no other default.

`make` takes alternating slot keywords and values. Missing required slots and
unknown keywords are errors.

A class has one abstract parent, specified by `:is`, or defaults to `Object`.
Abstract types can also have an abstract parent. They have no slots and cannot
be instantiated.

Concrete classes are final: you cannot derive another class from `Dog`. No slots
are inherited. To share stored data, put another object in a slot.

### Read and change slots

Instances support the same call-shaped reads and assignments as records:

```scheme
(set! (dog :age) 2)            ; => 2
(slot-set! dog :age 3)         ; => 3

(with-slots dog (age)
  (set! age (+ age 1))
  age)                        ; => 4
```

`with-slots` gives local names that read and write the object's slots. These are
live accesses, not copied values.

Instances have a fixed set of slots. They are not tables, so use slot access
instead of `get` or `put!`. Equality is by identity; `copy` makes a shallow
copy.

## Check membership and use built-in classes

Pass a class descriptor, without quoting it, to `is-a?`:

```scheme
(is-a? dog Dog)                ; => #t
(is-a? dog Animal)             ; => #t
(is-a? dog Object)             ; => #t
(is-a? cat Dog)                ; => #f
(is-a? (dog :age) Number)      ; => #t
(is-a? (dog :name) String)     ; => #t
```

Ordinary values have built-in dispatch classes. These include `Number` for
integers and floats, `String`, `Keyword`, `Vector` for arrays, `Hashtable` for
tables, and `Procedure`.

Built-in classes are final and have `Object` as their parent. You can specialize
methods on them, but cannot construct their values with `make`.

Use `instance?` to check for a user class instance. For an exact class check
without ancestors, compare `(dispatch-type value)` with the class using `eq?`.

## Define generic functions and methods

Declare a generic function's parameters, then add methods:

```scheme
(defgeneric (speak animal))

(defmethod (speak (animal Animal))
  "Silence.")

(defmethod (speak (animal Dog))
  "Bark!")

(defmethod (speak (animal Cat))
  "Meow.")

(speak dog)                   ; => "Bark!"
(speak cat)                   ; => "Meow."
```

A parameter written as `(animal Dog)` specializes that method on `Dog`. An
ordinary parameter name accepts any value, just like specializing on `Object`.

The most specific applicable method runs. A `Dog` method wins over an `Animal`
method, which wins over an `Object` method. If no method applies, the call
raises an error.

Generics have fixed arity. Methods must take the declared number of arguments;
optional and rest parameters are not supported.

### Dispatch on more than one argument

Methods can specialize any argument position:

```scheme
(defgeneric (meet first second))

(defmethod (meet (first Animal) (second Animal))
  :sniff)

(defmethod (meet (first Dog) (second Cat))
  :chase)

(meet dog cat)                ; => :chase
(meet cat dog)                ; => :sniff
```

Specificity considers every argument. Suppose `meet` had methods for
`(Dog, Animal)` and `(Animal, Cat)`. A call with `(dog, cat)` would be
ambiguous: each method is more specific in a different position. A `(Dog, Cat)`
method resolves that ambiguity. Argument order does not break ties.

## Call the next method

Use `:next` immediately after a method's signature to name a local next-method
procedure:

```scheme
(defgeneric (animal-details animal))

(defmethod (animal-details (animal Animal))
  '(animal))

(defmethod (animal-details (animal Dog))
  :next super
  (cons 'dog (super animal)))

(animal-details dog)          ; => (dog animal)
```

`super` is a name chosen by the method author. Pass every argument explicitly
when calling it. Its value is `#f` when no next method remains, so guard the
call when a fallback is optional.

A next-method call continues among the remaining methods applicable to the
original call. Passing different arguments does not start fresh dispatch or
bring in methods excluded from that call.

### Wrap methods with `:around`

An `:around` method wraps the primary method chain:

```scheme
(defmethod :around (animal-details (animal Animal))
  :next super
  (cons 'checked (super animal)))

(animal-details dog)          ; => (checked dog animal)
```

When several around methods apply, the most specific runs outermost. Calling its
next method proceeds through the remaining around methods, then the primaries.

An around method can return without calling its next method, skipping everything
inside it. The outermost around method supplies the final result. Otium has no
`:before` or `:after` methods.

## Keep instances across reloads

Reevaluating `defclass` or `defabstract` in the same namespace preserves the
type's identity. Existing instances and methods specialized on that type remain
usable.

For example, reevaluate `Dog` with an added slot:

```scheme
(defclass Dog :is Animal
  name
  (age :init 0)
  (tricks :init [])
  (vaccinated? :init #f))

(dog :vaccinated?)            ; => #f
(dog :age)                    ; => 4
```

Existing instances migrate when their slots are next accessed. Kept slots retain
their values, added slots receive their defaults, and removed slots disappear.
Changing a default does not overwrite an existing value. Changing an abstract
parent changes membership and dispatch for existing instances.

### Convert old data during migration

Specialize `update-instance` when a layout change needs a conversion. It
receives the instance with its new slots and a table of old slot values keyed by
keywords.

Here, a new required `display-name` slot gets its value from the old `name`:

```scheme
(defmethod (update-instance (animal Dog) old-slots)
  (set! (animal :display-name) (get old-slots :name)))

(defclass Dog :is Animal
  name
  display-name
  (age :init 0)
  (tricks :init [])
  (vaccinated? :init #f))

(dog :display-name)           ; => "Milo"
```

A new required slot must be filled by `update-instance` or migration fails. New
calls to `make` must supply it normally. If migration fails, the instance's
layout and slot values are restored, and a later access retries.

### Reload methods and records

Reevaluating `defgeneric` with the same arity preserves its methods. Defining a
method with the same specializers and qualifier replaces that method. A primary
method and an around method with the same specializers are separate definitions.

Deleting a method from source does not remove the installed method. Remove it
explicitly:

```scheme
(remove-method! animal-details (list Animal) :around) ; => #t
```

Records have stricter reload rules. Repeating the same declaration preserves the
record type and existing records. Changing fields, field order, or mutability is
an error. Use a new record name or restart the process for a changed layout.

For module reloads and retained bindings, see [Live
programming](guide/live-programming.md).

<!-- generated from classes.md.spec sha256:ede5686533d1fb17e683186f60e96554a282d749b70cc530a86d4364eab63a09 -->
