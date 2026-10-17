# Pattern matching

Not implemented. This is a design to come back to, not a commitment.

`match` takes a value apart by shape and binds the pieces. It's a macro in the
shared prelude, written in Otium, so both runtimes get the same behaviour from
the same code.

## Why

There's no destructuring today. `let` binds names, `fn` takes flat parameter
lists, and pulling apart a list, table or instance means a run of `car`, `get`
and slot reads. Code that looks at a condition, an event table or a message
ends up as a `cond` over type tests followed by those reads.

## Prior art

- [Racket `match`](https://docs.racket-lang.org/reference/match.html), for
  constructor-style patterns, `?`, `app` and `==`.
- [Janet `match`](https://janet-lang.org/docs/flow.html), for open table
  patterns and keeping the pattern language small.
- [core.match](https://github.com/clojure/core.match), for maps matching open.

## At a glance

```scheme
(match event
  ({:type :click :pos [x y]} (click! x y))
  ({:type :key :key k} :when (printable? k) (insert! k))
  ((Button :label l) (print l))
  ((list 'move dx dy) (move! dx dy))
  (_ nil))
```

## `match`

`(match expr clause…)` evaluates `expr` once, then tries each clause in order.
A clause is `(pattern body…)` or `(pattern :when guard body…)`. The first clause
whose pattern matches, and whose guard is truthy, runs its body with the
pattern's bindings in scope. The body is a body (language 4.1) and is in tail
position. The guard sees the pattern's bindings.

If no clause matches, `match` signals a `match-error` condition, a subtype of
`error`, holding the value under `:value`. A `use-value` restart returns its
argument as the result of the `match`.

## Patterns

| pattern | matches |
| --- | --- |
| `_` | anything, binds nothing |
| `name` | anything, binds `name` |
| `42`, `"s"`, `:kw`, `nil`, `#t`, `()` | a value `equal?` to the literal |
| `'datum` | a value `equal?` to `datum` |
| `(== expr)` | a value `equal?` to `expr`, evaluated in the enclosing scope |
| `(list p… )`, `(list p… & rest)` | a proper list of exactly that length, or at least that length with `rest` matched against the tail |
| `` `(datum …) `` | a list shaped like the template, where `,p` positions match `p` and everything else matches literally |
| `[p…]`, `[p… & _]` | an array of exactly that length, or at least that length |
| `{key p …}` | a table where each `key` is present and its value matches `p`; other keys are ignored |
| `(opt p)`, `(opt name default)` | inside a table pattern only: see below |
| `(Class :slot p …)` | an instance of `Class` whose named slots match |
| `(Abstract)` | an instance of any class under the abstract type |
| `(? pred p…)` | a value for which `pred` is truthy and which matches every `p` |
| `(app f p)` | a value where `(f value)` matches `p` |
| `(and p…)` | a value matching every `p`, left to right |
| `(or p…)` | a value matching any `p`; every alternative must bind the same names |

Literals compare with `equal?`, so `1` doesn't match `1.0`. Use
`(? (fn (v) (= v 1)))` for numeric equality.

Bare symbols always bind. A pattern never changes meaning because of what's in
scope; use `(== x)` to compare against an existing variable. Using the same name
twice in one pattern is an error at expansion time. It could become an equality
check later without breaking anything.

Table keys in a pattern must be literals. Because `nil` can't be stored in a
table, "present" means the lookup isn't `nil`, so `{:k v}` fails when `:k` is
missing. A table pattern also requires a table, so `{}` doesn't match `nil`.

Optional keys use `opt`. `{:k (opt p)}` matches `p` against the value when
`:k` is present, and binds `p`'s names to `nil` when it's missing.
`{:k (opt name default)}` binds `default` instead; `default` is evaluated only
when the key is missing.

Required keys were chosen over the language's usual "reads are total over
absence" rule (2.2). `match` is choosing a clause, and `{:name n}` quietly
matching a table with no `:name` usually picks the wrong one.

Quasiquote patterns are the short way to write tagged lists:

```scheme
(match msg
  (`(move ,dx ,dy) (move! dx dy))
  (`(say ,text) (print text)))
```

`,@` is an error for now.

Array rest patterns only accept `_`. `[a b & rest]` is an error at expansion
time, because binding the rest means copying the array, an allocation hidden
in a pattern. List rest patterns bind the tail without copying.

`[a b]` and `{:k v}` read as `(array a b)` and `(table :k v)`, so `match` sees
those calls and treats `array` and `table` as pattern heads. Patterns read the
way the values are built.

Class patterns read the class var when the match runs, not when it's expanded,
so redefining a class keeps working. A slot the class doesn't have is the same
runtime error as `(instance :slot)`.

## Pattern heads

A list pattern's head resolves in this order:

1. A core form: `?`, `app`, `and`, `or`, `==`, `opt`, `quote`,
   `quasiquote`, `list`, `array`, `table`.
2. A var holding a class or abstract type.
3. Otherwise it's an error at expansion time.

## Compilation

A `match` expands to straight-line tests and `let`s. It doesn't allocate a
closure per clause, and `(list a & rest)` binds the tail without copying.

`app` and `?` with a symbol or literal `fn` are inlined: `(app car a)` becomes
`(let ((t (car v))) …)`. Without that, user patterns would cost more than
built-in ones and people would avoid them.

Neighbouring clauses that test the same shape may share the test. Whether a
`?` or `app` function runs again for a later clause is unspecified, so they
should be pure. A full decision tree can wait until benchmarks ask for it.

The core forms are kept few so that user-defined patterns can be added later
without reworking the compiler.

## Not included

- Destructuring in `let` and `fn` parameters. That changes special forms on
  both runtimes and can come later as sugar over `match`. It's also a way to
  get optional and keyword arguments, which the language doesn't have (3.4),
  from patterns over the rest list instead of a separate feature. That needs
  `opt` to work at the end of a list pattern, `(fn (a & (list (opt b 0))) …)`,
  and a `keys` pattern over alternating keyword/value lists that `make` could
  share.
- `if-match` and `when-match`. `(match e (p then) (_ else))` is one pair of
  parens longer. They're easy to add once code shows the need.
- User-defined patterns. A `defpattern` that rewrites patterns into other
  patterns, stored in var metadata so a function and a pattern can share a
  name, is the likely shape.
- Repetition patterns like Racket's `...`.
- Exhaustiveness checking.
