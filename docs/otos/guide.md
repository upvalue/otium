# ✨ otos guide

otos is a small operating system for Otium. It is a unikernel: one program, the Otium C runtime, linked with a thin kernel layer into a single RISC-V image. It boots in QEMU straight into an Otium REPL on the serial console.

This guide covers building, booting and changing otos. For the language itself, see the [language guide](../language-guide.md). For the rest of the docs, see the [docs index](../README.md).

## What works today

- Booting on QEMU's `virt` machine as a 32-bit RISC-V (`rv32gc`) kernel, started by OpenSBI.
- An Otium REPL on the UART console, with line editing, history and multi-line input.
- `^C` interrupts running code. `^D` on an empty line shuts the machine down.
- Requiring namespaces from the `lib/` tree, which is built into the image as read-only files.
- Reading those files with ports, for example `read-file`.
- Drawing on a virtio-gpu display from the `otos.gfx` namespace.
- Monotonic time from the RISC-V `time` CSR, and wall-clock time from QEMU's Goldfish RTC.

There is no disk, network, writable file system or FFI. See [Limits and what is missing](#limits-and-what-is-missing).

## Layout

| Path | What it is |
| --- | --- |
| `otos/Justfile` | `build`, `run` and `run-display` recipes |
| `otos/meson.build` | Builds picolibc, compiler-rt builtins, the runtime and the kernel |
| `otos/cross/riscv32.txt` | Meson cross file with the target settings for `riscv32-unknown-elf` |
| `otos/cross/homebrew.txt`, `otos/cross/llvm-21.txt` | Toolchain cross files: LLVM from Homebrew, or LLVM 21 from Debian and Ubuntu packages |
| `otos/src/boot.S` | Entry point and trap vector |
| `otos/src/kernel.ld` | Linker script and memory layout |
| `otos/src/kernel.c` | `kmain`, the REPL, faults and SBI shutdown |
| `otos/src/ot-kernel.c` | Platform layer: UART console and its interrupt, clocks, and the I/O host over the embedded files |
| `otos/src/gfx.c` | The `otos.gfx` native procedures |
| `otos/src/display.c`, `otos/src/virtio.c` | virtio-gpu display driver |
| `otos/src/line.c`, `otos/src/line.h` | Line editor |
| `otos/src/kernel.h` | Console, clock and shutdown functions shared by the kernel files |
| `otos/src/files.h` | Layout of the embedded file table |
| `otos/tools/embed-files.py` | Generates the embedded file table |
| `otos/vendor/compiler-rt/` | Vendored compiler-rt builtins |
| `otos/subprojects/picolibc.wrap` | Fetches picolibc |

Two pieces of third-party code are used:

- **picolibc** 1.8.12 is the C library. Meson clones it from GitHub through `otos/subprojects/picolibc.wrap` into `otos/subprojects/picolibc/`. That directory is git-ignored. It is built single-threaded, without its own startup code (`picocrt=false`), without semihosting, and with `double` printf formatting.
- **compiler-rt builtins** live in `otos/vendor/compiler-rt/`. They are the generic sources from LLVM `llvmorg-22.1.8`, without the Apple and trampoline files. They provide the 64-bit integer and soft-float helpers that rv32 code calls. They are vendored because Homebrew LLVM does not ship a riscv32 `libclang_rt.builtins.a`.

## Prerequisites

- **LLVM** with clang, `lld`, `llvm-ar`, `llvm-strip` and `llvm-objcopy`. The build uses C23 and `#embed`, so clang must support both. LLVM 21 works. `otos/cross/homebrew.txt` points at `/opt/homebrew/opt/llvm/bin`, and `otos/cross/llvm-21.txt` at `/usr/lib/llvm-21/bin`. For another install, add a cross file like these two.
- **Meson** 1.6 or later, and Ninja.
- **Python 3**, to run `otos/tools/embed-files.py`.
- **git**, so Meson can fetch picolibc on the first setup.
- **QEMU** with `qemu-system-riscv32` (the `qemu-system-riscv` package on Debian and Ubuntu). QEMU's bundled OpenSBI firmware is used (`-bios default`). Some distributions package that firmware separately, as `opensbi`.
- **just**, to run the recipes. You can also run the commands by hand.

## Build and boot

Run the recipes from the `otos/` directory:

```sh
cd otos
just build   # meson setup (first time only) and meson compile
just run     # build, then boot in QEMU
```

`just build` picks the toolchain file: `homebrew` on macOS, `llvm-21` elsewhere. `just toolchain=NAME build` uses `cross/NAME.txt` instead. If `otos/build/` does not exist yet, it runs

```sh
meson setup build --cross-file=cross/riscv32.txt --cross-file=cross/llvm-21.txt
```

then `meson compile -C build`. The kernel is `otos/build/otos.elf`.

`just run` boots it with:

```sh
qemu-system-riscv32 -machine virt -m 256M -bios default -nographic -no-reboot -kernel build/otos.elf
```

After OpenSBI's banner you should see something like:

```
otos: hart 0, device tree at 0x8fe00000
otos: runtime ready in 410 ms
otium repl (otos) - ^C interrupts, ^D shuts down
ot>
```

To leave, press `^D` on an empty line or call `(exit)`. Either one shuts the machine down through SBI. If the kernel hangs, QEMU's own escape `^A x` quits.

`just run-display` adds a virtio-gpu device and opens QEMU's display window. The REPL stays in the terminal.

The default build type is `debugoptimized`. To change Meson options, reconfigure the existing build directory, or delete `otos/build/` and run `just build` again.

## How it boots

### Memory layout

`otos/src/kernel.ld` places the kernel at `0x80200000`. On QEMU `virt`, RAM starts at `0x80000000`, and OpenSBI takes the first 2 MB. The script assumes 256 MB of RAM. That must match the `-m 256M` flag in `otos/Justfile`, so change both together.

In order, the image contains:

- `.text`, with `.text.boot` kept first so `_start` is at the load address
- `.rodata`
- `.data`, which also defines `__global_pointer$`
- `.bss`, between `__bss_start` and `__bss_end`
- a 4 MB stack ending at `__stack_top`
- the heap, from `__heap_start` to `__heap_end` at the end of RAM

`.eh_frame` is discarded. picolibc's `malloc` takes memory from the heap region. The Otium runtime gets its memory through `malloc` too.

### Entry

OpenSBI jumps to `_start` in `otos/src/boot.S` in supervisor mode, with the hart id in `a0` and the device tree address in `a1`. `_start` then:

1. Loads `gp` with linker relaxation turned off, and sets `sp` to `__stack_top`.
2. Turns the FPU on by setting `sstatus.FS`, and clears `fcsr`. This must happen before any compiled code runs, because the ABI is `ilp32d` and the compiler may use float registers anywhere.
3. Points `stvec` at `trap_entry`.
4. Zeroes `.bss`.
5. Calls `kmain(hart, device_tree)`. If `kmain` ever returned, the hart would wait in a `wfi` loop.

### Kernel entry

`kmain` in `otos/src/kernel.c`:

1. Prints the hart id and device tree address. The device tree is not parsed.
2. Turns on the UART receive interrupt with `console_init`.
3. Creates the runtime with `ot_create`, sets the heap limit, and installs the kernel's I/O host with `ot_set_io_host`.
4. Loads the expander and the core library from `/lib`, defines the `otos.gfx` natives, and gives the runtime the console's interrupt flag. It prints how long all that took.
5. Runs the REPL, and shuts down when it returns.

### Console

The console is the 16550 UART at `0x10000000`.

- `console_write` waits for the transmit register to be empty before each byte, and turns `\n` into `\r\n`.
- Input is interrupt-driven. The UART's receive interrupt (IRQ 10) goes through the PLIC to supervisor mode. The handler moves waiting bytes into a 256-byte ring buffer.
- `console_read_byte` takes a byte from the ring, and waits with `wfi` when it is empty.
- picolibc's `stdin`, `stdout` and `stderr` are all one stream backed by these functions, so `printf` and `puts` go to the UART.

### Traps

`trap_entry` checks `scause`. For an interrupt, it saves the caller-saved integer and float registers and `fcsr`, calls `kernel_interrupt`, restores them and returns with `sret`. Only the PLIC's external interrupt is enabled, and the only source is the UART.

Every exception is fatal. `trap_entry` resets `sp` to the top of the stack, in case `sp` caused the fault, then calls `kernel_fault` with `scause`, `sepc` and `stval`. `kernel_fault` prints:

```
otos: trap cause=... pc=... value=...
```

and then shuts down with a failure reason.

### Shutdown

`sbi_shutdown` makes an `ecall` to the SBI System Reset extension (`0x53525354`) to shut the machine down. It passes "system failure" as the reason when asked to report one, and no reason otherwise. picolibc's `exit` and `abort` end in `_exit`, which calls `sbi_shutdown` with failure set when the status is not 0. The REPL reports failure when it ends because of `(exit n)` with a non-zero `n`. Because QEMU runs with `-no-reboot`, a reset ends the QEMU process. QEMU's own exit status is 0 either way.

## How the runtime is embedded

### What is built from `c/`

`otos/meson.build` builds a static library, `otium`, from the runtime sources in `c/src/`, the same list as `c/meson.build` without `main.c`. The kernel includes the runtime's public header, `c/include/otium.h`.

The runtime is compiled with:

| Define | Meaning |
| --- | --- |
| `OTIUM_FREESTANDING` | No libffi, dlopen, threads or POSIX file calls. Files come from the I/O host |
| `OTIUM_BOOTSTRAP_FILE="/c/src/primitives.ot"` | Where the runtime reads its bootstrap library, inside the embedded files |
| `_POSIX_MONOTONIC_CLOCK` | Makes picolibc's `time.h` define `CLOCK_MONOTONIC` |

With `OTIUM_FREESTANDING`:

- FFI builtins raise an `ffi-error` with reason `unavailable`. Requiring `otium.ffi` fails with "foreign calls are not available in this build".
- `file-exists?`, `file-info`, `list-directory`, `delete-file`, `rename-file`, `make-directory`, `file-symbolic-link?` and `current-directory` raise a `file-error` ("Function not implemented"). So do `file?` and `directory?`, which use them.
- The runtime reads library and module files through the I/O host, the same one ports use.

The runtime is also 32-bit here. Integers are 30 bits, as on any 32-bit target, and `current-jiffy` counts milliseconds since boot (`jiffies-per-second` is 1000).

`kmain` sets these limits:

| Setting | Value | Meaning |
| --- | --- | --- |
| `HEAP_INITIAL` | 1 MB | Initial size of each semispace |
| `HEAP_LIMIT` | 16 MB | Largest each semispace can grow to |
| `STACK_LIMIT` | 128K | VM value slots |

### The platform layer

`otos/src/ot-kernel.c` provides:

- The console functions above, and picolibc's standard streams.
- `clock_gettime`, which picolibc declares but leaves to the platform. `CLOCK_MONOTONIC` reads the `time` CSR and assumes QEMU `virt`'s 10 MHz timebase instead of reading it from the device tree. `CLOCK_REALTIME` reads the Goldfish RTC at `0x101000`. The runtime's `current-jiffy` and `current-second` use these.
- `kernel_io_host`, the runtime's I/O host, which serves the console and the embedded files.

### Embedded files

At build time, `otos/tools/embed-files.py` writes `files.c` into the build directory, from `c/src/primitives.ot` and the repository's `lib/` directory. It skips dot files. Each file becomes a byte array filled with `#embed` and ended with a NUL byte. The table, laid out as in `otos/src/files.h`, maps each absolute path to its data and length. For example, `lib/otium/json.ot` becomes `/lib/otium/json.ot`, and the bootstrap library is `/c/src/primitives.ot`.

The script also writes a depfile that lists the directories as well as the files. So editing, adding or removing a file under `lib/` regenerates the table on the next `just build`.

The I/O host in `otos/src/ot-kernel.c` presents the table as a read-only file system:

- Paths are resolved against `/`, which is the only working directory. `.`, `..` and repeated slashes are handled.
- A directory is any path prefix of an embedded file. Opening one fails with `EISDIR`.
- Up to 16 files can be open at once. Their handles start at 3.
- Handles 0, 1 and 2 are the console. Only handle 0 can be read, and only handles 1 and 2 can be written. Reads from handle 0 return one byte at a time, turn `\r` into `\n`, and treat `^D` as end of input. They do not echo.
- Opening a file for writing fails with `EROFS`.

### Loading namespaces

`ot_load_core` adds `/lib` to the runtime's module search path, so `(require otium.json)` loads `/lib/otium/json.ot`. The runtime also tries `.` and `lib`, which resolve to `/` and `/lib` here. Only namespaces under `lib/` can be required. See the [language guide](../language-guide.md) for how `require` and namespaces behave.

### otos.gfx

`otos/src/gfx.c` defines the `otos.gfx` namespace with `ot_define_natives`. It has no source file under `lib/`, so `(require otos.gfx)` just makes the natives available.

| Procedure | What it does |
| --- | --- |
| `(size)` | The display's `(width height)` in pixels |
| `(clear color [alpha])` | Fills the screen |
| `(fill-rect x y width height color [alpha])` | Blends a rectangle over the screen, clipped to it |
| `(flush)` | Copies the framebuffer to the host and shows it |

Colors are `0xRRGGBB` with an optional alpha from 0 to 255, since rv32 integers stop at 30 bits and can't hold `0xRRGGBBAA`. The display is opened on first use. Without a virtio-gpu device, each procedure fails with "no display; run with just run-display".

## The REPL

The REPL lives in `repl` in `otos/src/kernel.c`.

- The prompt is `ot> `. While a form is still incomplete, it changes to `..> `, and each new line is added to the pending input. The runtime's `ot_scan_form` finds where each form ends.
- Each complete form is evaluated with `ot_eval`. Its value is printed with `ot_write`, so strings show in quotes.
- When evaluation fails, otos prints `error: ` followed by the runtime's message.
- `^C` at the prompt throws away pending input.
- `^C` while code is running interrupts it, and the REPL prints `interrupted`. While a form is evaluating, the UART interrupt handler turns `^C` into the runtime's interrupt flag and drops any typeahead. The runtime checks the flag at safe points. A blocked read from the console, such as `(read-line)`, is interrupted too.
- `(exit)` ends the REPL and shuts down, as does `^D` on an empty line.

A short session:

```lisp
(def (square x) (* x x))
(square 12)
(require otium.extra :as extra)
((extra/partial + 1) 2)
```

Each form's value is printed under it. `(square 12)` prints `144`.

## Line editing

`otos/src/line.c` is a small line editor. It handles control keys and the usual escape sequences for arrow, Home, End and Delete keys:

| Key | Action |
| --- | --- |
| Left, `^B` / Right, `^F` | Move one character |
| Home, `^A` / End, `^E` | Move to start / end of line |
| Backspace | Delete before the cursor |
| Delete, `^D` | Delete at the cursor (`^D` on an empty line is end of input) |
| `^K` | Delete to end of line |
| `^U` | Delete to start of line |
| `^W` | Delete the previous word (spaces, tabs and parentheses end a word) |
| Up, `^P` / Down, `^N` | Walk history |
| `^L` | Clear the screen |
| `^C` | Cancel the line |

Details:

- History holds 100 lines. Empty lines, and lines that repeat the previous entry, are not added. The line being typed is kept while you walk history, and Down brings it back.
- Each line holds at most 1023 bytes. Extra input is dropped.
- Tab inserts a space. Other control characters are ignored.
- UTF-8 is kept together when moving and deleting. The editor assumes each character is one column wide and the line fits on one terminal row. After each edit it redraws the whole line, except when typing at the end of the line.

## Making changes

- **Otium code under `lib/`:** edit it and run `just run`. The file table is regenerated, and `require` sees the new file. Because the files are baked in, `:reload` in a running kernel reloads the same code. To pick up an edit, rebuild and boot again.
- **Kernel C code:** add new source files to the `executable('otos.elf', ...)` list in `otos/meson.build`.
- **Runtime sources:** a file added to `c/meson.build`'s `runtime_sources` must also be added to `runtime_names` in `otos/meson.build`.
- **Native procedures:** define them with `ot_define_natives`, as `otos/src/gfx.c` does, and call the registration from `kmain`.
- **Runtime limits:** change `HEAP_INITIAL`, `HEAP_LIMIT` and `STACK_LIMIT` in `otos/src/kernel.c`.
- **Memory size:** change `LENGTH` in `otos/src/kernel.ld` and `-m` in `otos/Justfile` together.
- **Toolchain paths:** add or edit a file in `otos/cross/`, then delete `otos/build/` so Meson sets up again.

otos builds the C runtime, which is experimental and can lag behind the Chez Scheme runtime. Code that works on Chez may need features the C runtime does not have yet.

## Limits and what is missing

- **One machine.** Only QEMU `virt` on rv32 is supported. The UART, PLIC, RTC and timebase addresses are hard-coded, and the device tree is ignored.
- **One hart, one interrupt.** Only the UART's receive interrupt is enabled. There is no timer, no scheduler and no threads. Output and the display are polled. Any exception stops the machine.
- **No memory protection.** The kernel runs in supervisor mode with paging off, and the runtime shares one address space with everything else.
- **Small, fixed memory.** Each semispace of the Otium heap stops at 16 MB, and the VM stack at 128K slots.
- **Read-only files.** Only `lib/` is available, and only as it was at build time. Nothing can be written, and there is no disk driver. The filesystem procedures, such as `file-exists?` and `list-directory`, are not available.
- **Few devices.** Besides the console there is the virtio-gpu display. There is no network, input or block device.
- **No FFI.** `otium.ffi` is unavailable, so namespaces that depend on it (for example SDL, SQLite and Objective-C) cannot work.
- **Plain error messages.** Errors print the runtime's message, not the rendered diagnostics with source lines that `otium.diagnostics` gives on the host.
- **No development server.** The live tools used with `--dev` on the host are not available. Changes reach the kernel only by rebuilding.
- **Bounded integers.** Like the C runtime on any host, otos has no bignums, and on rv32 integers stop at 30 bits. See the [language guide](../language-guide.md).
- **Simple console editing.** Lines longer than one terminal row, and wide characters, are not drawn correctly.
