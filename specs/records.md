# Records

Records hold a fixed set of fields, stored inline in a managed object. Records have no inheritance or
methods. The existing class system is separate.

```scheme
(defrecord Point (x y))
(def p (record-new Point 10 20))
(p :x)                          ; 10
(record-ref p :y)               ; 20
(set! (p :x) 30)
(record-set! p :y 40)

(defrecord Chord (key modifiers) :immutable #t)
(def c (record-new Chord 65 4))
```

Construction takes positional values in field order. Every field is required;
missing or extra values are errors. Field names must be distinct symbols, and
access uses the corresponding keywords. Unknown fields are errors. Empty records
are allowed. `record?` tests instances, `record-type?` tests descriptors, and
`record-type` returns an instance's descriptor. `type` returns `:record` or
`:record-type`. Records are callable with one field keyword, including through
`apply`. They are not tables.

Fields are mutable unless the declaration has `:immutable #t`. Immutability is
shallow: an immutable field may hold a mutable array, table, or record. Both
`record-set!` and applied assignment reject writes to immutable records.

Equality and table-key hashing use identity, including for immutable records.
Mutation does not change a record's identity hash. Records are not automatically
interned. A cache can look up a compact integer before calling `record-new`, so a
cache hit needs no candidate record. For example, a normalized SDL keycode and
four modifier bits can identify a cached chord. This facility does not change the
UI's existing string keymaps.

## Reloading

A declaration's identity is its namespace and name. Evaluating the same field
list and mutability again returns the existing descriptor, so retained instances
remain usable across file reloads. The same local name in another namespace
identifies a different record type.

Adding, removing, or reordering fields, or changing mutability, raises an error
before replacing the binding. Use a new name or restart the process for a changed
layout. Existing instances are not migrated. Defaults, structural equality,
serialization, and automatic interning are outside this initial implementation.
