# otium/os

this is a simple operating system kernel; currently it targets QEMU RISC-V and supports purely
cooperative scheduling

## Testing

The kernel uses snapshot testing to verify behavior. Tests are defined in `test-snapshot.py` and run specific kernel configurations by compiling with test flags.

### Running tests

```bash
# Run all tests
./test-snapshot.py

# Update snapshots after making changes
./test-snapshot.py --update
```

### Test infrastructure

- `compile-riscv.sh` - Compiles the kernel with optional test flags (e.g., `--test-hello`)
- `test-snapshot.py` - Snapshot test runner that captures TEST: output lines
- `snapshots/` - Directory containing expected output for each test

### Available test modes

Tests are defined by kernel compile flags:

- `KERNEL_PROG_TEST_HELLO` - Runs a simple hello world process that prints and exits
- Default (no flag) - Runs the shell program

### Adding new tests

1. Add a new test mode in `otk/kernel.cpp` using `#ifdef` directives
2. Add the test to `TEST_PROGRAMS` dict in `test-snapshot.py`
3. Run `./test-snapshot.py --update` to create the snapshot
4. Test output lines must start with `TEST:` to be captured
