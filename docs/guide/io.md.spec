---
title: "Files and I/O"
prompt: |
  Write a guide to reading and writing files and text in Otium for a
  programmer who knows some Lisp and is learning Otium. They should come away
  knowing what a port is and how to read and write files and strings.
---

## Sources

`specs/io.md` (the "At a glance" and "Ports" sections), `lib/otium/resource.ot`,
`lib/otium/json.ot`, `tests/library/ports.ot`, `tests/library/read-file.ot`.

Read AGENT-LANG.md first. Check every example against the sources and the
tests; don't cite tests in the doc.

## Cover

Open with the problem ports solve before saying what they are. A program
reads and writes text in many places: files, the terminal, strings it is
building. Ports give all of these one interface, so a function that writes to
a port works the same whether the port is a file, standard output or a string,
and you can test it against a string. Reading through a port also lets a
program work through a large file a piece at a time instead of loading it
whole. The current input and output ports (dynamic params, see
[Dynamic params](params.md)) let a caller send a function's output somewhere
else without changing the function. The reader should understand why they'd
pass a port around before the first example.

Then, as one piece of work that builds up:

1. Reading a whole file with `read-file`.
2. Opening a file port with `with` so it is closed when the body exits, even
   on an error. Reading it line by line until the EOF object.
3. Writing a file: `write-string`, `newline`, and that opening an output file
   replaces it unless you ask to append.
4. String ports for building text in memory, and capturing output with
   `with-output-to-string` or by rebinding the current output port.
5. `display`, `write` and `println`. Say plainly that unlike Scheme,
   `display` does not take a port; use `display-to` and `write-to`.
6. What happens when a file can't be opened, with a pointer to
   [Errors and recovery](errors.md).

End with two short sections:

- Binary data: one paragraph saying bytevectors and byte ports exist, with a
  pointer to the reference. No tables.
- JSON: `json/parse` and `json/encode` with one example of reading and one of
  writing a file, and that JSON `null` is `json/null`.
