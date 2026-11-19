# ✨ Files and I/O

A program reads and writes text in many places: files on disk, the terminal,
and strings built in memory. Without a shared interface, code written for a
file has to be rewritten to print to the terminal or to run against a string
 in a test.

Ports give all of these sources and destinations one interface. A port is an
input stream or an output stream, backed by a file, standard input or output, a
string, or a bytevector. A function that takes a port works the same way on all
of them, and reading through an input port lets a program process a large file
a piece at a time instead of loading the whole file into memory.

Most I/O functions take a port as an optional last argument. When you leave it
out, they use [`current-input-port`](/reference/otium.core/#current-input-port)
or [`current-output-port`](/reference/otium.core/#current-output-port). Both
are dynamic params (see [Dynamic params](params.md)), so a caller can redirect a
function's input or output to another port without changing the function.

## Read and write a whole file

When a file is small enough to hold in memory at once,
[`write-file`](/reference/otium.core/#write-file) writes a string to a path and
[`read-file`](/reference/otium.core/#read-file) reads the whole file back as a
string:

```otium
(write-file "tasks.txt" "buy milk\nfix build\nwrite docs\n")

(read-file "tasks.txt")
;; => "buy milk\nfix build\nwrite docs\n"
```

Both functions open a file port, do their work, and close the port before
returning.

## Read a file line by line

To work through a file without loading all of it at once, open an input port
with [`open-input-file`](/reference/otium.core/#open-input-file). Wrap it in
[`with`](/reference/otium.core/#with) so Otium closes the port when the block
exits, whether the body returns normally or unwinds on an error.

[`read-line`](/reference/otium.core/#read-line) reads one line at a time,
stripping the line ending. When the port reaches the end of the file, read
functions return the end-of-file value. Test for it with
[`eof-object?`](/reference/otium.core/#eof-object?):

```otium
(with ((in (open-input-file "tasks.txt")))
  (let loop ((line (read-line in))
             (found '()))
    (if (eof-object? line)
        (reverse found)
        (loop (read-line in)
              (if (string-contains? line "build")
                  (cons line found)
                  found)))))
;; => ("fix build")
```

In Otium, only `nil` and `#f` are false. The EOF object is truthy, so always
check it with `eof-object?` rather than `if` or `when` alone.

## Write to a file port

[`open-output-file`](/reference/otium.core/#open-output-file) opens a port for
writing. Use [`write-string`](/reference/otium.core/#write-string) to write
text and [`newline`](/reference/otium.core/#newline) to end a line:

```otium
(def (write-entry line port)
  (write-string "- " port)
  (write-string line port)
  (newline port))

(with ((out (open-output-file "tasks.txt")))
  (write-entry "fix build" out))
```

Opening an output file creates it if it does not exist and replaces its
contents if it does. To add to the end of an existing file instead, pass
`{:if-exists :append}`:

```otium
(with ((out (open-output-file "tasks.txt" {:if-exists :append})))
  (write-entry "ship release" out))

(read-file "tasks.txt")
;; => "- fix build\n- ship release\n"
```

## Build and read strings with ports

String ports read from and write to text in memory. Because `write-entry` takes
a port, you can test it with
[`open-output-string`](/reference/otium.core/#open-output-string)
and [`get-output-string`](/reference/otium.core/#get-output-string) without
touching the filesystem:

```otium
(with ((out (open-output-string)))
  (write-entry "test in memory" out)
  (get-output-string out))
;; => "- test in memory\n"
```

[`open-input-string`](/reference/otium.core/#open-input-string) turns a string
into an input port that works with the same reading functions as a file:

```otium
(with ((in (open-input-string "alpha\nbeta")))
  (list (read-line in) (read-line in)))
;; => ("alpha" "beta")
```

When a function writes to the current output port instead of taking a port
argument,
[`with-output-to-string`](/reference/otium.core/#with-output-to-string)
runs a zero-argument function with `current-output-port` bound to a fresh
string port and returns the collected string:

```otium
(with-output-to-string
  (fn ()
    (write-string "captured")
    (newline)))
;; => "captured\n"
```

You can do the same redirection yourself with
[`with-params`](/reference/otium.core/#with-params) when you want to send
output to a file port or an existing string port:

```otium
(with ((out (open-output-string)))
  (with-params ((current-output-port out))
    (write-string "redirected"))
  (get-output-string out))
;; => "redirected"
```

## Print values

[`display`](/reference/otium.core/#display),
[`write`](/reference/otium.core/#write), and
[`println`](/reference/otium.core/#println) format values to
`current-output-port`. `display` prints strings and characters without quotes
or escapes, `write` prints a readable representation with quotes and character
prefixes, and `println` acts like `display` followed by a newline. When given
several arguments, they separate them with spaces:

```otium
(with-output-to-string
  (fn ()
    (display "path:" "/tmp")
    (newline)
    (write "path:" "/tmp")
    (newline)
    (println "done" 2)))
;; => "path: /tmp\n\"path:\" \"/tmp\"\ndone 2\n"
```

Unlike Scheme, `display` and `write` are variadic and never take a port
argument; passing a port to `display` prints the port value itself. To print to
a specific port, use [`display-to`](/reference/otium.core/#display-to) or
[`write-to`](/reference/otium.core/#write-to), which take the port first:

```otium
(with ((out (open-output-string)))
  (display-to out "status" :ok)
  (newline out)
  (write-to out "status" :ok)
  (get-output-string out))
;; => "status :ok\n\"status\" :ok"
```

## Handle file errors

When a file cannot be opened, Otium signals a `file-error` condition table with
the failing path under `:path` and a keyword such as `:not-found` or
`:permission` under `:reason`. Catch it with `try` and
[`type-pred`](/reference/otium.core/#type-pred):

```otium
(try
  (read-file "missing.txt")
  (catch ((type-pred 'file-error) err)
    (list (err :reason) (err :path))))
;; => (:not-found "missing.txt")
```

See [Errors and recovery](errors.md) for more on condition tables, handlers,
and restarts.

## Binary data

Otium represents fixed-length byte sequences as bytevectors, written
`#u8(79 116 105 117 109)`. Every port can read and write individual bytes or
bytevectors alongside text, and in-memory byte ports work like string ports.
See [`bytevector`](/reference/otium.core/#bytevector),
[`open-input-bytevector`](/reference/otium.core/#open-input-bytevector),
[`open-output-bytevector`](/reference/otium.core/#open-output-bytevector),
[`read-u8`](/reference/otium.core/#read-u8),
[`read-bytevector`](/reference/otium.core/#read-bytevector),
[`write-u8`](/reference/otium.core/#write-u8), and
[`write-bytevector`](/reference/otium.core/#write-bytevector) in the
[otium.core](/reference/otium.core/) reference.

## JSON

The [otium.json](/reference/otium.json/) module converts between Otium values
and JSON text. JSON objects become tables with string keys, JSON arrays become
arrays, and JSON `null` is represented by
[`json/null`](/reference/otium.json/#null)
so a key holding `null` can live in a table without being deleted as `nil`.

Use [`json/encode`](/reference/otium.json/#encode) to turn a value into a JSON
string for writing to a file, and [`json/parse`](/reference/otium.json/#parse)
to parse JSON text back into Otium values:

```otium
(require otium.json :as json)

(write-file "config.json"
            (json/encode {"host" "localhost"
                          "port" 8080
                          "tls" json/null}))

(read-file "config.json")
;; => "{\"host\":\"localhost\",\"port\":8080,\"tls\":null}"

(let ((config (json/parse (read-file "config.json"))))
  (list (config "host")
        (config "port")
        (eq? (config "tls") json/null)))
;; => ("localhost" 8080 #t)
```

<!-- generated from io.md.spec sha256:d46f3750684481cfbf0d73ce02b06ccd4c4cb03afcb70f200ee71ca12ba79d7c -->
