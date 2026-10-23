# Bestline

Vendored from https://github.com/jart/bestline at commit
`395fa5d1680a1caaef60d903e71f7ee7a62d292b`.

The upstream source is distributed under the BSD-2-Clause license in
`LICENSE`.

Local extension: `bestlineSetEditorCallback` handles Ctrl-X Ctrl-E. Bestline
suspends raw mode around the callback and replaces the buffer only when the
callback returns an allocated string. The caller owns editor launching.

History files keep ordinary entries as plain lines. Multiline entries (and
entries beginning with byte 0x1e) use a 0x1e prefix followed by hex-encoded
bytes, so one entry occupies one physical line. Loading accepts both formats.
