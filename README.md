# otium

Otium is a little operating system project that I worked on from September to December of 2025.

It's a simple microkernel that runs on RISC-V (under QEMU) or on WebAssembly.

Features:
- The shell is a small implementation of [Tcl](https://en.wikipedia.org/wiki/Tcl) so normal
  programming works
- Graphics, filesystems and input devices support are just processes, which can crash normally
  without impacting the overall system
- A cooperative scheduler, which is implemented using Emscripten fibers on WebAssembly

## Screenshots

Tcl shell with taskbar showing running processes:

![Tcl Shell](assets/tcl-shell.png)

Graphics demo with concentric circles:

![Graphics Demo](assets/graphics-demo.png)

## Links

- [otium.sh](https://otium.sh) - website
- [devlog](https://upvalue.io/posts/tag/otium) - dev log

