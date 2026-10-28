---
title: How to run
description: Getting started with running Otium code
---

You'll need the following dependencies to run Otium:

- A POSIX system. Otium is developed primarily on OSX and occasionally tested
  on Linux.
- A c compiler. Only clang has been tested so far.
- Meson for building the C code.
- SDL3, SDL3_ttf. 

There are a few other C libraries Otium uses, which are vendored in and should
be built along side it. In theory most of the C libraries are optional but the
build configurations that exclude them and the scripts around them aren't
routinely tested.

Check out Otium from its GitHub repository

> git clone https://github.com/upvalue/otium otium
> cd otium

And then you can build the C code with

> meson setup build 
> meson compile -C build

Once that's done, use the CLI wrapper to enter the REPL and enjoy!

> otium

Or run an example program

> ./otium examples/hand/lc-1-two-sum.ot

<!-- LLMs should not edit this document! -->
