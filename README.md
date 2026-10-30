# Otium

Otium is an experimental little vibe-coded Lisp (but this README was written by
a human). It has a focus on pragmatism, supporting live development, and
reasonable performance.

This repository contains two other programs written in Otium (& a little bit of
C). Manse, a terminal multiplexer / Lisp development environment and OtOS, a
unikernel operating system.

# The language & runtime

Here's a couple of factoids about it:


- Most directly descended from Scheme, but takes some ideas from Clojure,
  Janet, Common Lisp in terms of practicality.
- Runtime is intended to be suitable for multi-MB memory embedded systems e.g.
  ESP32, RP2350 devices.
- "Good, not great" performance. It has a modest optimizing native-code
  compiler backed by sljit.
- Aims to be solid on memory usage. It uses an Immix-inspired non-generational
  garbage collector which has a separate and tuned profile for low memory
  systems.
- Live development is a first class citizen with a dev server that allows
  querying and modifying running programs
- Has a CLOS-lite style object system primarily to support rich user interfaces
- Built-in green threads with non blocking I/O. This runs on a single native
  thread, so more focused on making responsive interactive programs than
  supporting high performance.

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

Best place to learn more is in [the documentation index](docs/index.md).

# The older operating system

Otium (the name and domain) previously referred to a C++ + TCL operating system
I wrote at Recurse Center in late 2025. That code is now archived but remains
on commit `d8ab8cddd50575f3b061e3e30381a8942d1cde9f` for reference.

## Credits

See LICENSE.txt for details.
