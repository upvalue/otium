# Emscripten Fibers Cooperative Multithreading Example

This example demonstrates transparent cooperative multithreading using Emscripten's fiber API.

## Description

The program contains two "subprograms" (fibers) that cooperatively yield control to each other:
- **Fiber 1**: Prints "1" then "3"
- **Fiber 2**: Prints "2" then "4"

Each fiber calls `yield()` after printing, which transparently transfers control between them without either fiber needing to know about the other. The result is sequential output: `1 2 3 4`

## Building

```bash
# Build both JS and HTML versions
make

# Or build just the JS version
make main.js

# Or build just the HTML version
make main.html
```

## Running

### Node.js
```bash
make run
# or
node main.js
```

### Browser
Open `main.html` in a web browser and check the browser console for output.

## How It Works

1. **Fiber Setup**: Two fibers are created with separate C stacks and Asyncify stacks
2. **Scheduler**: A simple round-robin scheduler alternates between fibers
3. **Yield Function**: A transparent `yield()` function swaps control back to the scheduler
4. **Execution Flow**:
   - Scheduler runs fiber1 → prints "1" → yields
   - Scheduler runs fiber2 → prints "2" → yields
   - Scheduler runs fiber1 → prints "3" → yields
   - Scheduler runs fiber2 → prints "4" → yields
   - Both fibers complete

## Key Technical Details

- Compiled with `-sASYNCIFY=1` to enable fiber support
- Each fiber has its own C stack (2MB) and Asyncify stack (2MB)
- The scheduler fiber is created from the main execution context
- Fibers yield control without knowledge of other fibers (transparent cooperation)

## Requirements

- Emscripten SDK (emcc)
