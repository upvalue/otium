# otium/os

This is a simple operating system with a kernel and a user-space program.

Currently, it targets three systems:
- RISC-V under QEMU with assembly to switch and store stack information by hand
- WASM on Emscripten, using the fiber library to switch processes
- POSIX; portions of the operating system (not the whole thing) can run on OSX
  for standalone utilities (Tcl interpreter) or unit tests

## Coding style

In general, try to avoid using #ifdef based on platform architecture outside of the
platform-specific files (those containing riscv or wasm in the name).

## Source

The source of the operating system is written in C++. Source files live in the `./ot` directory.
Because kernel and user space programs may be compiled and run separately, `ot/kernel` contains
kernel code and `ot/user` contains user code. `ot/shared` contains shared process-agnostic code
including some C stdlib like functionality that can be run in either system.

## Subsystems

### Processes and scheduler

The operating system runs multiple processes. Processes have a PID (unsigned int), a name and a
state of UNUSED, RUNNABLE or TERMINATED.

The scheduler currently is a purely cooperative system where processes yield to the operating
system, which then selects the next process by PID and runs it.

When a process encounters a fault on RISC-V, the process will be terminated by the OS.

### Memory

The memory is a simple page system. Pages are kept in a free list. Processes never free pages, but
when processes are killed or exit the pages are returned to the OS. Pages can be readable, writable
and executable and processes can only access pages directly belonging to them, but this enforcement
only happens on RISC-V (as WASM doesn't have a mechanism for doing so).

## Testing

### Unit tests

There are some unit tests with doctest that run on the host system; these can be run with
`./test-unit.sh`.
 
### Snapshot tests

The kernel uses snapshot testing to verify behavior. Tests are defined in `test-snapshot.py` and run
specific kernel configurations by compiling with test flags.

#### Running tests

```bash
# Run all tests
./test-snapshot.py

# Update snapshots after making changes
./test-snapshot.py --update
```

#### Test infrastructure

- `compile-riscv.sh` - Compiles the kernel with optional test flags (e.g., `--test-hello`)
- `test-snapshot.py` - Snapshot test runner that captures TEST: output lines
- `snapshots/` - Directory containing expected output for each test

#### Available test modes

Tests are defined by kernel compile flags:

- `KERNEL_PROG_TEST_HELLO` - Runs a simple hello world process that prints and exits
- Default (no flag) - Runs the shell program

#### Adding new tests

1. Add a new test mode in `otk/kernel.cpp` using `#ifdef` directives
2. Add the test to `TEST_PROGRAMS` dict in `test-snapshot.py`
3. Run `./test-snapshot.py --update` to create the snapshot
4. Test output lines must start with `TEST:` to be captured

## Tcl

The operating system includes a minimal, self-contained Tcl interpreter implementation that runs in userspace. The interpreter is designed to work in a freestanding environment without standard library dependencies.

### Design

The Tcl implementation (`ot/user/tcl.h` and `ot/user/tcl.cpp`) provides:

- **Custom container classes**: `tcl::string`, `tcl::string_view`, and `tcl::vector<T>` that use only `malloc`/`free`/`realloc`
- **No standard library dependencies**: Uses placement new and custom allocators instead of `<string>`, `<vector>`, etc.
- **Lightweight parser**: Handles Tcl syntax including variables (`$var`), command substitution (`[cmd]`), and string quoting
- **Interpreter**: Maintains command registry, variable scopes (call frames), and evaluation state

### Memory Management

The Tcl interpreter uses the TLSF (Two-Level Segregated Fit) memory allocator:

1. The shell allocates contiguous pages from the OS (currently 10 pages = 40KB)
2. TLSF provides `malloc`/`free`/`realloc` implementations on top of this pool
3. All Tcl objects (strings, vectors, commands) allocate from this pool
4. Memory allocation failures are detected and cause graceful exit with error messages

### Core Commands

The interpreter includes these built-in commands:

- **I/O**: `puts` - print output
- **Variables**: `set` - get/set variables
- **Control flow**: `if`, `while`, `break`, `continue`, `return`
- **Procedures**: `proc` - define new commands
- **Arithmetic**: `+`, `-`, `*`, `/`
- **Comparison**: `==`, `!=`, `>`, `<`, `>=`, `<=`

### Shell Program

The shell (`ot/user/prog-shell.cpp`) provides an interactive REPL:

1. Initializes TLSF memory pool on startup
2. Creates Tcl interpreter and registers core commands
3. Reads input character-by-character (supports backspace)
4. Evaluates complete lines when Enter is pressed
5. Displays results or error messages
6. Custom commands can be added (e.g., `quit`, `crash` for testing)

### Usage Example

```tcl
> set x 10
result: 10
> set y 20
result: 20
> + $x $y
result: 30
> proc double {n} { return [* $n 2] }
result:
> double 5
result: 10
```

### Standalone REPL

A POSIX-compatible standalone Tcl REPL can be built for testing on the host system:

```bash
./build-tcl-repl.sh
./bin/tcl-repl              # Interactive mode
./bin/tcl-repl script.tcl   # Run script
```

This uses the same Tcl core but with POSIX I/O and the bestline library for line editing.
