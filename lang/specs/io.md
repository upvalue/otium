# I/O

Draft. Nothing here is implemented yet.

Ports for files, standard streams, strings and bytes, on both runtimes. To
support them, Otium gains a character type and a bytevector type, and `first`
and `get` on strings return characters instead of one-character strings.

## Why

Today Otium can print to the host's output and read a whole file into a string
(`read-file`). There is no way to read standard input, stream a file that
doesn't fit in memory, read or write binary data, or send output anywhere other
than the host sink. `GAPS.txt` lists the missing streaming and directory APIs,
and notes that invalid UTF-8 and filesystem errors behave differently on C and
Chez.

A streaming JSON reader and writer is the motivating example. It isn't part of
this spec, but the port design should make it straightforward and reasonably
fast. See [JSON](#json) below.

## Prior art

- [R7RS](https://small.r7rs.org/attachment/r7rs.pdf) section 6.13, for names and
  argument order.
- R6RS and Chez, for custom ports. Chez's ports are also what the Chez runtime
  builds on.
- [Janet](https://janet-lang.org/api/file.html), for reading into a
  caller-supplied buffer, and for `ev`, which shows how blocking-style port code
  can later run under an event loop.
- Go's `bufio`, for bulk scanning operations (`ReadSlice`, `ReadString`) that
  keep lexers out of per-byte loops.

## Relationship to R7RS

Names and argument order follow R7RS wherever there is an R7RS equivalent, so
Scheme code and the `benchmarks/r7rs` ports mostly carry over. Ports take the
port as an optional final argument, defaulting to the current input or output
port.

Where Otium differs:

- Errors are condition tables (section 8 of `specs/language.md`), not R7RS
  error objects.
- Text is always UTF-8. There are no transcoders or encoding options.
- A port is not split into textual and binary kinds. Every port is a byte
  stream, and text operations decode UTF-8 from the same buffer, so they can be
  mixed freely.
- `display`, `write` and `println` stay variadic and always write to the current
  output port. `(display x port)` prints both values. Use `write-string`, or
  rebind `current-output-port`.
- Left out: `call-with-port`, `call-with-input-file` and the other
  `call-with-` forms. Use `with`.
- Additions: custom ports, bulk scanning (`read-until`, `read-while`,
  `skip-while`), `read-all`, `open-ports`, and the general `with` form.

End of file is an EOF object, as in R7RS. `(eof-object)` returns it and
`eof-object?` tests for it. It is truthy, like every value other than `nil` and
`#f`.

## At a glance

```scheme
(with ((in (open-input-file "access.log"))
       (out (open-output-file "errors.log")))
  (let loop ((line (read-line in)))
    (unless (eof-object? line)
      (when (string-contains? line " 500 ")
        (write-string line out)
        (newline out))
      (loop (read-line in)))))

(with-output-to-string
  (fn () (println "captured")))          ; => "captured\n"

(let ((p (open-input-string "  42, 43")))
  (skip-while " " p)
  (read-while "0123456789" p))           ; => "42"
```

## Characters

A new immutable type, `char`, holding one Unicode scalar value (a code point
that isn't a surrogate).

| | |
| --- | --- |
| Literals | `#\a`, `#\space`, `#\newline`, `#\tab`, `#\return`, `#\nul`, `#\x41` |
| repr | `#\a`, using the named form where there is one |
| display | the character itself |
| `(type c)` | `:char` |
| Equality | immediate, so `eq?` and `equal?` agree |
| Ordering | `sort` gains chars as a fifth orderable class (2.5), by code point |

Functions:

- `(char? x)`
- `(char->integer c)`, and `(integer->char n)`, which errors on surrogates and
  values above `#x10FFFF`
- `(char=? c c…)`, `(char<? c c…)`, `(char<=? …)`, `(char>? …)`, `(char>=? …)`
- `(char-upcase c)`, `(char-downcase c)`, which may be ASCII-only, like the
  string versions (Appendix A)
- `(char-alphabetic? c)`, `(char-numeric? c)`, `(char-whitespace? c)`
- `(digit-value c)`, giving 0 to 9, or `nil`

Changes to strings:

- `first`, `last`, `nth` and `get` on a string return a char, or `nil` on a
  miss. Today they return a one-character string.
- `(string-ref s k)` is like `get`, but out of range is an error.
- `(string c…)` builds a string from chars.
- `(string->list s)` and `(list->string l)` convert to and from a list of chars.
- `str`, `buffer-push!` and `string-join` already use display forms, so chars
  work in them with no change: `(str #\a "bc")` is `"abc"`.
- `(string-ref s k)` and `get` stay O(k) on C, which stores strings as UTF-8
  (2.7). For a left-to-right scan, use `open-input-string` and read
  characters. That is O(n) overall.

Before switching `first` and `get`, check which shared code and examples
depend on them returning strings. Nobody has checked yet.

### Representation

On C, chars are immediates and never allocate. The low two bits `10` already
mark immediates, and only values below 32 are in use (`nil` 2 through
`OT_TOMBSTONE` 26). A char is `(codepoint << 5) | 30`, so `(v & 31) == 30`
identifies one. That needs 26 bits, which fits on 32-bit targets. `ot_is_ptr`
is false for chars, so the collector ignores them. The EOF object takes the
next free constant, 34.

On Chez, chars are Chez chars and the EOF object is Chez's. Chez Otium's `get` on strings currently wraps
`string-ref` in `string` (`chez/otium/values.sls`). It will stop doing that,
and dispatching on chars in generic functions becomes portable
(`docs/portability.md`).

## Bytevectors

A new mutable type for fixed-length runs of bytes, `bytevector`.

| | |
| --- | --- |
| Literal | `#u8(1 2 255)` |
| repr | `#u8(1 2 255)` on both runtimes |
| `(type b)` | `:bytevector` |
| Equality | identity, like arrays (2.4) |

Functions:

- `(bytevector n…)`, `(make-bytevector k [fill])`, `(bytevector? x)`
- `(bytevector-length b)`, `(bytevector-u8-ref b k)`,
  `(bytevector-u8-set! b k n)`
- `(bytevector-copy b [start [end]])`,
  `(bytevector-copy! to at from [start [end]])`, `(bytevector-append b…)`
- `(utf8->string b [start [end]])`, which raises `decode-error` on invalid
  UTF-8, and `(string->utf8 s [start [end]])`
- The generic functions `length`, `count`, `get` and `put!` work as they do on
  arrays.

On C, the bytes live inline in a heap object. On Chez, these are Chez
bytevectors, printed by Otium's printer as `#u8`, not `#vu8`.

## Ports

### The port value

`port` is a new opaque type, printed `#<port "access.log" input>` or
`#<port stdout output>`. A port is for input or for output, not both. Later,
sockets can be represented as a pair of ports.

Every port is buffered and holds:

- a source or sink: a host file, a string, a bytevector or a custom port,
- a byte buffer,
- an open or closed flag,
- a name for printing and errors.

Text operations decode and encode UTF-8 directly from the byte buffer. Peeking
a character decodes without moving the read position, so peeking a character
and then calling `read-u8` returns that character's first byte. Nothing needs
a separate character lookahead.

| Predicate | |
| --- | --- |
| `(port? x)`, `(input-port? x)`, `(output-port? x)` | |
| `(input-port-open? p)`, `(output-port-open? p)` | |

### Opening and closing

| Form | Behaviour |
| --- | --- |
| `(open-input-file path)` | |
| `(open-output-file path [opts])` | Creates the file. `opts` is a table: `:if-exists` is `:truncate` (default), `:append` or `:error`. `:buffer-size` sets the buffer size in bytes. |
| `(open-binary-input-file path)`, `(open-binary-output-file path [opts])` | Aliases, since ports have no separate binary kind. |
| `(open-input-string s)`, `(open-output-string)`, `(get-output-string p)` | |
| `(open-input-bytevector b)`, `(open-output-bytevector)`, `(get-output-bytevector p)` | |
| `(close-port p)`, `(close-input-port p)`, `(close-output-port p)` | Flushes, then releases the host resource. Closing an already closed port does nothing. |
| `(with-output-to-string f)` | Calls `f` with `current-output-port` bound to a fresh string port and returns the string. |

`open-input-file` also accepts `:buffer-size` as an optional table. The
default size is implementation-defined. A low-memory host should be able to
choose a few hundred bytes.

Any operation on a closed port raises `io-error` with `:reason :closed`.

Ports dispatch as the built-in class `Port`, and `close` (see [`with`](#with))
has a method for it that calls `close-port`. Use `with` to scope a port.

### Standard ports

`current-input-port`, `current-output-port` and `current-error-port` are
params (section 9). Call them to read them, as R7RS does, and rebind them with
`with-params`:

```scheme
(with-params ((current-output-port log)) (run))
```

The defaults wrap standard input, output and error. On C, the default output
port writes through the host writer (`ot_set_writer`), so embedders that
capture output keep working. `display`, `write`, `print`, `println` and
`newline` write to `(current-output-port)`. `newline` gains an optional port.

### Input

Each one takes an optional port, defaulting to `(current-input-port)`.

| Form | Result |
| --- | --- |
| `(read-char [p])` | A char, or the EOF object. |
| `(peek-char [p])` | Same, without consuming the char. |
| `(read-line [p])` | A string without its `\n`, or a preceding `\r`. The EOF object at EOF. A final line with no newline is returned as-is. |
| `(read-string k [p])` | Up to `k` chars, or the EOF object. |
| `(read-all [p])` | The rest of the input as a string. `""` at EOF. |
| `(read-u8 [p])`, `(peek-u8 [p])` | A byte, as an int, or the EOF object. |
| `(read-bytevector k [p])` | Up to `k` bytes, or the EOF object. |
| `(read-bytevector! b [p [start [end]]])` | Reads into `b` and returns the count, or the EOF object. Allocates nothing. |
| `(read [p])` | Reads one Otium datum, or returns the EOF object. |

`read-file` stays, as shorthand for `with` plus `read-all`.

All of these read at least one item when one is available. They block only
when the buffer is empty. A read stops early only at EOF.

### Bulk scanning

These are Otium additions so that lexers can do most of their work in native
code. Each takes a stop set, a string of characters, then an optional port:

| Form | Result |
| --- | --- |
| `(read-until stops [p])` | The chars before the first char in `stops`, which is left unread. |
| `(read-while chars [p])` | The longest prefix of chars in `chars`. |
| `(skip-while chars [p])` | Like `read-while`, but discards the chars and returns the count. |

They always return a string, possibly empty, even at EOF. Use `peek-char` to
tell what stopped the scan. That keeps the usual loop free of EOF checks.
Implementations can turn an ASCII stop set into a bitmap, so a scan costs one
table lookup per byte with no calls back into Otium.

### Output

Each one takes an optional port, defaulting to `(current-output-port)`.

| Form | |
| --- | --- |
| `(write-char c [p])` | |
| `(write-string s [p [start [end]]])` | `start` and `end` are char positions, so on C they cost O(end). |
| `(write-u8 n [p])` | |
| `(write-bytevector b [p [start [end]]])` | |
| `(flush-output-port [p])` | |

Flushing:

- Output is flushed when the buffer fills, on `flush-output-port`, and on
  close.
- The default output port flushes on newline when it is a terminal, and before
  any read from the default input port.
- The default error port is unbuffered.
- On a normal exit (end of script, `exit`, `quit`), the runtime flushes every
  open output port. It doesn't wait for GC to do it.

### Custom ports

```scheme
(make-input-port name {:read (fn (bytes start end) count-or-eof)
                       :close (fn () …)})
(make-output-port name {:write (fn (bytes start end) count)
                        :flush (fn () …)
                        :close (fn () …)})
```

Custom ports work only at the byte level. The port does its own buffering and
UTF-8 decoding, so text behaves identically on every kind of port. `:read`
fills `bytes` between `start` and `end` and returns how many bytes it wrote,
at least one, or the EOF object. `:close` and `:flush` are optional. The REPL's
editor connection and future FFI-backed sockets are the intended users.

### Open port registry

`(open-ports)` returns an array of the ports that are currently open, excluding
string and bytevector ports. This exists for reloading. A file that opens a
port at top level and gets reloaded leaks the old port, and this is how to see
that from the REPL. Ports kept across reloads belong in `defonce`.

GC closes host resources for unreachable ports, through the ext finalizer on C
and a guardian on Chez. Programs shouldn't rely on that. Finalizers run at
different times on each collector and runtime, and an unflushed port collected
this way loses its buffered output.

### Errors

New condition types, registered with `define-condition`:

- `io-error`, under `error`
- `file-error`, under `io-error`
- `decode-error`, under `io-error`

A condition table looks like this:

```scheme
{:type 'file-error
 :message "open-input-file: not found: data.json"
 :op 'open-input-file
 :path "data.json"
 :reason :not-found
 :detail "No such file or directory"}
```

`:reason` is one of `:not-found`, `:exists`, `:permission`, `:is-directory`,
`:not-directory`, `:no-space`, `:closed` or `:other`. Both runtimes produce the
same `:reason`. `:detail` is the host's message, and it may differ.

`decode-error` adds `:position`, the byte offset in the port, and is raised
for invalid UTF-8, including input that ends partway through a sequence. Both
runtimes use Otium's decoder, so their decoding behaviour is the same.

## `with`

Not specific to I/O. Ports are its first user, but it releases any resource
when control leaves a block: native memory, SDL handles, locks, transactions.
It works like Python's `with`. The scope is the dynamic extent of the block. A
closure that keeps a resource can still reach it after release, and using it
then is an error wherever the resource tracks its own state.

```scheme
(with ((name expr [release]) …)
  body…)
```

- Bindings are made in order, like `let`, and each is visible to the ones
  after it.
- When the block exits, each acquired resource is released in reverse order.
  That happens on every exit path: normal return, an error unwinding, a
  restart, an interrupt or `quit`. It is built on `unwind-protect`.
- If a binding's expression raises an error, only the resources already
  acquired are released.
- With no `release`, a resource is released by calling the generic function
  `close`. With a `release`, `(release resource)` is called instead and
  `close` is not involved.
- A resource with no applicable `close` method and no `release` is an error
  when it is bound, not when it is released. Silently skipping it would hide
  mistakes.
- `with` returns the body's value. An error from releasing replaces the
  block's result, as with an `unwind-protect` cleanup (8.7).

```scheme
(defgeneric close (resource outcome))
```

`outcome` is `nil` for a normal exit, the condition when an error is unwinding,
or `:quit` for interrupts and `quit`. Most methods ignore it. It's there so a
transaction can commit or roll back, or a temporary file can be kept or
deleted. Unlike Python's `__exit__`, `close` can't suppress the error. Use
`try` or a handler for that.

Methods are provided for:

- `Port`, which calls `close-port`,
- `Pointer`, for owning FFI pointers, which calls `ffi/free!`.

User classes add their own. Anything without a class, such as a raw pointer, a
table or an integer descriptor from a C library, uses the `release` element:

```scheme
(with ((point (ffi/alloc Point 1))
       (window (create-window "demo" 640 480) destroy-window))
  …)
```

Releasing twice is harmless for ports and owning pointers, and using either
after release is an error. A raw pointer released through `release` has no
such check. `ffi/own` fixes that: `(ffi/own pointer destroy)` returns an
owning pointer whose `close` calls `destroy` and marks it released.

On Chez, a continuation captured inside the block can jump back into it after
release. Re-entering a `with` block is unsupported.

`Port` and `Pointer` are new built-in classes (`specs/classes.md`). FFI
pointers currently have no dispatch identity of their own. On Chez they fall
into `Other`.

## Filesystem

A small set of functions, all raising `file-error`:

| Form | |
| --- | --- |
| `(file-exists? path)` | |
| `(delete-file path)`, `(rename-file from to)` | |
| `(file-info path)` | `{:type :file :size 1234 :modified 1.7e9}`, where `:type` is `:file`, `:directory` or `:other` |
| `(list-directory path)` | An array of entry names, sorted, without `.` and `..` |
| `(make-directory path)` | |

## Implementation

### C

Port I/O goes through host functions, alongside `ot_platform_read_file`:

```c
typedef struct ot_io_host {
  int  (*open)(void* userdata, const char* path, unsigned flags, intptr_t* handle);
  long (*read)(void* userdata, intptr_t handle, uint8_t* bytes, size_t length);
  long (*write)(void* userdata, intptr_t handle, const uint8_t* bytes, size_t length);
  int  (*close)(void* userdata, intptr_t handle);
  /* stat, rename, unlink, list, mkdir */
} ot_io_host;

void ot_set_io_host(ots* state, const ot_io_host* host, void* userdata);
```

- Failures return a reason code that the runtime maps to a `:reason`.
- `ot-posix.c` supplies the default, using file descriptors. Using descriptors
  rather than `FILE*` means they can be polled later.
- An embedder with no filesystem can install a host whose `open` always fails
  with `:not-found`.
- A port is a heap object with a finalizer. Its byte buffer is allocated with
  `ot_host_alloc` outside the heap, so moving the port during GC doesn't move
  the buffer the OS is reading into. This touches allocation and roots, so run
  `make gc-lint`.
- Blocking reads must be interruptible. When `read` returns `EINTR`, the port
  checks the interrupt hook before retrying, so `^C` at a prompt waiting on
  input still works.

### Chez

Ports are an Otium record that wraps a Chez binary port and holds a bytevector
buffer, filled with `get-bytevector-some!`. UTF-8 decoding, line splitting and
the scanning functions are written in Scheme against that buffer, not taken
from Chez's textual ports. That takes more code than using Chez's transcoders,
but it is how decoding errors, peek behaviour and buffering end up the same on
both runtimes. Chez's I/O conditions are mapped to `:reason`.

### Order of work

1. Characters and the EOF object on both runtimes, plus the string changes
   and an audit of existing callers.
2. Bytevectors.
3. `with` and the `close` generic, with methods for owning FFI pointers and
   `ffi/own`. This doesn't depend on ports and can go first.
4. File, string and bytevector ports with input, output and errors, tested in
   `shared/tests/library` on both runtimes.
5. Moving `display` and friends onto `current-output-port`, with
   `ot_set_writer` as the default output port's sink.
6. Custom ports, `open-ports`, and the filesystem functions.
7. A JSON library as a separate piece of work to test the design. Benchmark
   it against `read-file` plus a string-based parser on both runtimes.

## JSON

Not part of this spec. These are notes on how a JSON library would use ports,
to check the design against.

### Reading

A recursive descent parser over a port, peeking one char to choose what comes
next:

```scheme
(def ws " \t\r\n")

(defn read-value (p)
  (skip-while ws p)
  (let ((c (peek-char p)))
    (cond ((eof-object? c) (error "json: unexpected end of input"))
          ((eq? c #\{) (read-object p))
          ((eq? c #\[) (read-array p))
          ((eq? c #\") (read-json-string p))
          ((eq? c #\t) (expect-word p "true") #t)
          ((eq? c #\f) (expect-word p "false") #f)
          ((eq? c #\n) (expect-word p "null") :null)
          (else (read-number p)))))

(defn read-json-string (p)
  (read-char p)
  (let ((out (buffer)))
    (let loop ()
      (buffer-push! out (read-until "\"\\" p))
      (let ((c (read-char p)))
        (cond ((eq? c #\") (buffer->string out))
              ((eq? c #\\) (buffer-push! out (read-escape p)) (loop))
              (else (error "json: unterminated string")))))))

(defn read-number (p)
  (let ((s (read-while "0123456789+-.eE" p)))
    (or (string->number s) (error "json: bad number" s))))
```

Most of the input passes through `read-until`, `read-while` and `skip-while`,
which run natively. The Otium code runs once per token, not once per
character. `read-char` and `peek-char` allocate nothing on C, because chars
are immediates.

How much streaming this gives, by case:

- **One document from a file or pipe.** `(json/read p)` never holds the whole
  text in memory, only the result. That's the main win over `read-file`.
- **Many documents, as in NDJSON or log streams.** Call `json/read` until
  `peek-char` after whitespace returns the EOF object. Memory stays at one value.
- **One huge document.** Building the full result is the problem. A pull API,
  such as `(json/next parser)` returning `:object-start`, `:key`, `:string`,
  `:array-end` and so on, with the scalar available from
  `(json/value parser)`, lets the caller skip or stream subtrees. It's built
  from the same port operations. Returning the event as a keyword and keeping
  the scalar in the parser object avoids allocating per event.
- **Data arriving incrementally on a non-blocking source.** Not possible with
  blocking ports alone. Either the parser becomes a resumable state machine fed
  chunks (a push parser), or a future task system suspends the parser inside
  `read-u8` while it waits. The second keeps parser code like the code above,
  but the C interpreter would need to suspend a task in the middle of a native
  call. See [Open questions](#open-questions).

### Writing

`(json/write value p)` writes directly to the port, so there's no intermediate
string, and buffering makes the many small writes cheap. Strings need
escaping. Scanning a string by index is O(n²) on C, so the writer reads the
string through a string port instead:

```scheme
(def needs-escape
  (str "\"\\" (apply string (map integer->char (range 32)))))

(defn write-json-string (s p)
  (let ((in (open-input-string s)))
    (write-char #\" p)
    (let loop ()
      (write-string (read-until needs-escape in) p)
      (let ((c (read-char in)))
        (unless (eof-object? c)
          (write-string (escape c) p)
          (loop))))
    (write-char #\" p)))
```

That allocates one string per unescaped run. A port-to-port
`(copy-until stops in out)` would avoid that, if it turns out to matter.

### Problems that belong to the library

- **`null` in objects.** A table can't store `nil` (2.2), so `{"a": null}`
  needs a stand-in, such as `:null` above.
- **Keys.** Should keys come back as strings or keywords? Keywords look nicer
  in Otium, but they're interned and never freed, which is a bad fit for
  untrusted input.
- **Large integers.** Integers beyond 31 bits, or beyond the host's int width.
  Convert them to floats, keep the text, or raise an error.
- **`\u` escapes.** These encode UTF-16. Surrogate pairs combine into one
  char, and lone surrogates must be rejected or replaced, since
  `integer->char` refuses them.

## Deferred

- Non-blocking I/O, `char-ready?` and `u8-ready?`, and a task system
- Sockets and subprocesses
- Seeking (`port-position`, `set-port-position!`)
- Encodings other than UTF-8
- Replacing invalid UTF-8 instead of raising an error
- File locking, permissions, symlinks, and watching

## Open questions

1. Is a task system that can suspend inside a port read likely? If so, the C
   interpreter's native calls need a way to yield, and that should shape the
   C port internals now, even if nothing uses it yet.
2. Is `close` the right name for the generic? It reads well for ports and
   memory, less well for locks, where `release` would be clearer.
3. Should there be `display-to` and `write-to` variants that take the port
   first, so a printer doesn't need `with-params`?
4. Should `string-ref` be added at all? It makes an O(n) operation look cheap
   on C. `get` already covers it.
5. The `#u8(…)` literal reads as a constant, unlike `[…]` and `{…}`, which
   read as constructor calls. Is that an acceptable inconsistency?
