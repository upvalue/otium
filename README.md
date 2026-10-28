# Otium

Otium is an experimental little vibe-coded Lisp (but this README was written by
a human). It has a focus on pragmatism, supporting live development, and
reasonable performance.

This repository contains two other programs written in Otium (& a
little bit of C).

# The language

It is most directly descended from Scheme, but takes some ideas from Clojure
and Janet in terms of practicality, and a CLOS-lite sort of object system.

Its goal is being a fairly dynamic language with a runtime suitable for
multi-MB embedded systems (eg ESP32, RP2350) or as an extension language for C
programs. 

There are some examples in ./examples, including handwritten code, to show how
it feels to use.

The best place to learn more about the language is starting in
[./docs/index.md](./docs/index.md).

# Manse

Manse is a terminal multiplexer based on libghostty that uses scrolling (like
the wayland compositor Niri) as its terminal management paradigm.

# OtOS 

OtOS is a unikernel operating system for embedded devices in the multi-MB
memory range. Its userspace is written in the Otium language.

# Learn more

Best place to learn more is in docs/index.md.

# The older operating system

Otium (the name and domain) previously referred to a C++ + TCL operating system
I wrote at Recurse Center in late 2025. That code is now archived but remains
on commit `d8ab8cddd50575f3b061e3e30381a8942d1cde9f` for reference.

## Credits

See LICENSE.txt for details.
