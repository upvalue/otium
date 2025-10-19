# otium/os

This is a simple operating system with a kernel and a user-space program.

Currently, it targets two systems:
- RISC-V under QEMU with assembly to switch and store stack information by hand
- WASM on Emscripten, using the fiber library to switch processes

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
