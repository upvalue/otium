# SQLite

`otium.sqlite.raw` binds the SQLite C API through `otium.ffi`. It provides
connection and statement lifecycles, parameter binding, column access, errors,
transactions through SQL, synchronous `exec` callbacks, and online backup.
There is no query builder, row conversion, automatic cleanup, or exception
translation. SQLite failures return SQLite result codes. For the higher-level
API, see [managed connections and queries](#managed-connections-and-queries).

This is a subset of the [SQLite C API](https://www.sqlite.org/c3ref/intro.html).
Names drop `sqlite3_` and replace underscores with hyphens: `sqlite3_prepare_v2`
is `prepare-v2`. Constants drop `SQLITE_`: `SQLITE_ROW` is `ROW`. The module
includes all primary result codes, column types, common open flags, and limit
categories. Extended result codes are returned as integers. Functions keep
the C argument order and arity, including output parameters.

## Loading

```scheme
(require otium.ffi :as ffi)
(require otium.sqlite.raw :as sql)
(sql/load! '("libsqlite3.dylib" "libsqlite3.so.0" "libsqlite3.so"))
```

`load!` takes a library path or a list of candidates. Requiring the module
alone does not load SQLite; call `load!` before using its functions. It needs
a SQLite 3 library exporting the bound functions and the Otium native FFI helper. No SQLite build
dependency is added. A missing library or symbol raises an FFI error.

Reloading the module preserves the library and rebinds procedures. It leaves
connections and statements alive. Do not switch SQLite libraries while handles
from the old library are in use.

## A prepared statement

```scheme
(def db-out (ffi/alloc 'pointer 1))
(def stmt-out (ffi/alloc 'pointer 1))
(def source (ffi/alloc-utf8 "select 42"))
(defer
  (begin
    (let ((rc (sql/open ":memory:" db-out))
          (db (ffi/ref db-out 'pointer 0)))
      (defer
        (begin
          (unless (= rc sql/OK) (error "sqlite open" rc))
          (let ((rc (sql/prepare-v2 db source -1 stmt-out ffi/null))
                (stmt (ffi/ref stmt-out 'pointer 0)))
            (defer
              (begin
                (unless (= rc sql/OK) (error (sql/errmsg db)))
                (let ((step-result (sql/step stmt)))
                  (unless (= step-result sql/ROW) (error "sqlite step" step-result))
                  (println (sql/column-int64 stmt 0))))
              (sql/finalize stmt))))
        (sql/close db))))
  (ffi/free! source)
  (ffi/free! stmt-out)
  (ffi/free! db-out))
```

Check cleanup return codes too when they matter: `finalize` can report a prior
execution failure, and `close` returns `BUSY` while statements or backups are
still active. `close-v2` defers connection destruction until outstanding
resources are released. Even a failed open can return a handle that needs
closing. Empty SQL can prepare successfully with a null statement.

## Memory and types

Handles are opaque FFI pointers. Output parameters use `ffi/alloc 'pointer 1`
and `ffi/ref`. Use `ffi/null` for C NULL, including optional output arguments.
There is no validation of handle kind, statement state, or stale handles.
Using a finalized statement or closed connection has C's undefined behavior.

`prepare-v2` takes a pointer to SQL bytes, a byte count (`-1` for NUL-terminated
SQL), a statement output pointer, and an optional tail output pointer. The tail
points into the original SQL buffer; keep that buffer alive while reading it.
Using a pointer here avoids a dangling tail into a temporary FFI string.

`bind-text` and `bind-blob` take a buffer pointer, byte count, and destructor
argument after the statement and one-based parameter index:

- `TRANSIENT` asks SQLite to copy before returning. The buffer can then be freed.
- `STATIC` requires the buffer to remain alive until that parameter is rebound,
  bindings are cleared, or the statement is finalized. Resetting is insufficient.
- A destructor callback must stay rooted until SQLite has invoked it. Callback
  execution must follow the FFI's synchronous, same-thread restrictions.

Negative text lengths scan to NUL; blob lengths must be nonnegative. Use an
explicit text byte count for embedded NULs. A null buffer binds SQL NULL even
with length zero. Use a non-null buffer for empty text/blob, or `bind-zeroblob`
with zero for an empty blob. SQLite's [binding contract](https://www.sqlite.org/c3ref/bind_blob.html)
describes destructor behavior on errors.

Column indexes are zero-based. Read values only after `step` returns `ROW`.
`column-text` and `column-blob` return borrowed pointers; `column-bytes` gives
the length. Copy bytes before stepping, resetting, or finalizing, and before
another conversion invalidates the buffer. `ffi/read-utf8` stops at NUL;
read or copy the explicit byte span to preserve embedded NULs. A zero-length
blob can return a null pointer. Use `column-type` before conversions to
distinguish it from SQL NULL. See SQLite's [column lifetime rules](https://www.sqlite.org/c3ref/column_blob.html).

Metadata and error strings (`errmsg`, `column-name`, `sql`, etc.) are copied
into Otium strings. `exec`'s optional error-message output is different: it is
SQLite-owned memory that the caller must release with `sql/free`, not
`ffi/free!`. Connection and statement pointers likewise use `close` and
`finalize`, never `ffi/free!`.

`int64` calls preserve the full signed 64-bit range on Chez. The experimental
C runtime's integer range is narrower; its FFI raises a range error for values
outside that range. No integer is silently converted to a float.

## Scope and tests

UTF-16 APIs, user-defined SQL functions, virtual tables, extension loading,
hooks, incremental blob I/O, and variadic configuration APIs are not bound.
The module source lists the available functions and their FFI signatures.

Run `just test 'sqlite raw'` or `tests/run 'sqlite raw chez' 'sqlite raw c'`.
Tests use in-memory databases and skip when the system SQLite library is
absent. They cover binding ownership, embedded NULs, empty values, statement
reuse, errors, callbacks, backup, transaction state, and module reload.

## Managed connections and queries

`otium.sqlite` adds managed connections, prepared statements, transactions,
and a clause-based query builder. It loads the system SQLite library on the
first `open`; requiring it does not open a database. The implementation is
Otium and runs on Chez and C.

```scheme
(require otium.sqlite :as sql)
(require otium.resource :refer (with close))

(def minimum-age 18)
(def adults
  (sql/select [:id :name]
    (from :users)
    (where (>= :age minimum-age))
    (order-by :name)))

(sql/inspect adults)
;; {:sql "SELECT \"id\", \"name\" FROM \"users\" WHERE (\"age\" >= ?) ORDER BY \"name\""
;;  :parameters [18]}

(with ((db (sql/open "people.sqlite")))
  (sql/execute! db adults))
```

Keywords name SQL identifiers. Dots separate qualified names, so `:u.name`
becomes `"u"."name"`; `:*` and `:u.*` select all columns. Identifier parts are
quoted and embedded quotes are escaped. Values become positional `?`
parameters. Local variables are evaluated when the query is constructed,
not when it is executed. A query can be inspected or executed repeatedly.
Bytevector parameters are copied when constructing a fragment; `inspect`
also returns detached parameter storage.

`execute!` returns an array of row arrays, including rows from `RETURNING`.
Column order is the SELECT order. Arrays preserve duplicate column names
and SQL NULL values. NULL is `nil`, integers stay integers, floats stay
floats, text becomes a string, and blobs become bytevectors. Text and blobs
are copied before the statement advances, including embedded NULs and empty
values. Boolean parameters bind as 1 or 0. Other parameter types are errors.
C's integer range still applies, as described above.

### Clauses and composition

`select` takes a nonempty column vector followed by clauses. Supported
clauses are `from`, `join`, `left-join`, `where`, `group-by`, `having`,
`order-by`, `limit`, and `offset`. Clauses compile in SQL order, regardless
of their order in the form. Multiple `where` or `having` clauses combine
with AND. Repeated ordering and grouping clauses append expressions;
`from`, `limit`, and `offset` must occur at most once.

```scheme
(def base (sql/select [:u.id :u.name] (from (as :users :u))))
(def filtered
  (sql/compose base
    (when minimum-age (sql/clause (where (>= :u.age minimum-age))))
    (order-by (desc :u.name))))
```

`compose` returns a new query, leaving `base` unchanged. A clause can be
stored with `sql/clause` and reused. `nil` and `#f` clauses are ignored.
Queries and fragments have library-owned fields; compose through the API
rather than changing their slots.

SQL expressions recognize comparisons (`=`, `!=`, `<>`, `<`, `<=`, `>`,
`>=`), arithmetic (`+`, `-`, `*`, `/`), `and`, `or`, `not`, `like`, `is`,
`is-not`, `in`, `exists`, `as`, `asc`, and `desc`. SQL functions include
`count`, `sum`, `avg`, `min`, `max`, `total`, `coalesce`, `lower`, `upper`,
`length`, `abs`, and `round`. Use `(is :column nil)` to test for NULL.
Within these forms, the names have their SQL meanings. Compute a local
variable first when you want the corresponding Otium calculation instead.
Other Otium expressions are evaluated normally and their results become
parameters, identifiers, fragments, or subqueries.

```scheme
(def eligible
  (sql/select [:id] (from :users) (where (>= :age minimum-age))))

(def totals
  (sql/select [:u.name (as (count :p.id) :posts)]
    (from (as :users :u))
    (left-join (as :posts :p) (= :p.author_id :u.id))
    (where (in :u.id eligible))
    (group-by :u.id :u.name)
    (having (> (count :p.id) 0))
    (order-by (desc (count :p.id)))))
```

A query value in an expression becomes a parenthesized subquery. It can
also be a FROM or JOIN source with `(as query :alias)`. `in` accepts a
subquery or a literal vector of expressions, such as `(in :id [1 2 3])`.
`sql/expr` builds a reusable expression fragment.

Inserts take one column vector and one value vector of equal length:

```scheme
(sql/insert-into :users [:name :age] ["Ada" 37]
  (returning [:id :name]))
```

This first pass has no UPDATE/DELETE builder, multi-row insert syntax, ORM,
or migration system. Use raw SQL for those operations.

### Raw SQL and prepared statements

`(sql/raw text value ...)` is the escape hatch. Its text is trusted SQL;
its values are still bound parameters. Raw fragments can be complete
statements or expressions inside a query. Do not interpolate untrusted
values into the text.

```scheme
(with ((db (sql/open ":memory:"))
       (statement (sql/prepare db (sql/raw "select ? as value" "first"))))
  (sql/columns statement)                 ; ["value"]
  (sql/step! statement)                   ; ["first"]
  (sql/step! statement)                   ; nil
  (sql/bind! statement ["second"])
  (sql/step! statement))                  ; ["second"]
```

`prepare` accepts a query or raw fragment, compiles exactly one statement,
and binds all parameters. Empty statements and non-whitespace tails are
rejected, including a comment following the statement's final semicolon.
SQL text and database paths containing NUL are rejected. Parameter count
must match exactly; use positional `?` placeholders in raw fragments.

`bind!` resets a statement and replaces every binding. After a binding
failure has begun changing bindings, `step!` rejects the statement until a
complete `bind!` succeeds. After DONE, further `step!` calls return `nil`
until rebinding. This prevents SQLite from automatically rerunning a
completed write. `columns` returns copied column names.

SQLite failures raise condition tables with `:type 'sqlite-error`,
`:message`, `:operation`, and `:code`; execution errors also include
`:extended-code`. Closed-resource errors have `:reason :closed`.

### Lifetimes and transactions

Use `otium.resource/with` to scope connections and statements. Explicit
`(close resource nil)` is also supported. Closing a connection finalizes all
its outstanding managed statements, newest first. Close is idempotent;
operations on a closed connection or statement raise an error before
calling SQLite. Do not close or finalize managed handles through the raw
API. There is no GC finalizer: callers must close resources deterministically.

```scheme
(with ((db (sql/open "people.sqlite"))
       (transaction (sql/transaction db)))
  (sql/execute! db
    (sql/insert-into :users [:name :age] ["Ada" 37])))
```

The transaction commits on normal scope exit and rolls back on an error,
restart, quit, or interrupt. A failed commit attempts rollback and preserves
the commit error. Nested transactions are rejected in this pass. Keep
transaction control inside the scope API; manual BEGIN/COMMIT/ROLLBACK in a
managed transaction can invalidate the scope's ownership of the transaction.

Module reload preserves class identities and existing handles. Existing
connections, statements, and transactions use the reloaded methods. Keep
long-lived resources in `defonce` bindings; redefining a variable that owns
a connection still requires closing its previous value.

Run `tests/run 'sqlite high-level chez' 'sqlite high-level c'` for the managed
API, query builder, and resource-scope checks.
