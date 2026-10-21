# SQLite

`otium.sqlite.raw` binds the SQLite C API through `otium.ffi`. It provides
connection and statement lifecycles, parameter binding, column access, errors,
transactions through SQL, synchronous `exec` callbacks, and online backup.
There is no query builder, row conversion, automatic cleanup, or exception
translation. SQLite failures return SQLite result codes.

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
