# JavaScript Support Interface for WASM Builds

This document describes the JavaScript interface required to run Otium OS under WebAssembly. The WASM build uses Emscripten's `EM_JS` mechanism to call JavaScript functions from C++ code. These functions must be provided on the `Module` object before the WASM module is initialized.

## Overview

The OS expects the following categories of callbacks:

1. **Console I/O** - Text output and character input
2. **Time** - System clock
3. **Graphics** - Framebuffer initialization and rendering
4. **Filesystem** - File and directory operations
5. **Lifecycle** - Exit and abort handlers

## Required Module Callbacks

### Console I/O

#### `Module.print2(text: string): void`

Write text to the console output. Called by `oprintf` and related functions.

- **text**: String to output (no trailing newline implied)
- Should write to stdout or equivalent display

```javascript
Module.print2 = function(text) {
  process.stdout.write(text);  // Node.js
  // or: outputElement.textContent += text;  // Browser
};
```

#### `Module.printErr(text: string): void`

Write error text to the console.

- **text**: Error message to output
- Should write to stderr or display prominently

```javascript
Module.printErr = function(text) {
  console.error('ERROR:', text);
};
```

#### `Module.inputBuffer: number[]`

Array of character codes for keyboard input. The OS polls this buffer via `ogetchar()`.

- Push ASCII/Unicode character codes to provide input
- Push `13` (CR) for Enter/newline
- The OS will `shift()` characters as it reads them

```javascript
Module.inputBuffer = [];

// Example: feeding a line of input
function sendLine(text) {
  for (let i = 0; i < text.length; i++) {
    Module.inputBuffer.push(text.charCodeAt(i));
  }
  Module.inputBuffer.push(13);  // Enter
}
```

### Time

#### `Module.time_get(): number`

Get the current time in milliseconds.

- Returns: Current timestamp (milliseconds since epoch or arbitrary base)
- Used for frame timing and delays

```javascript
Module.time_get = function() {
  return Date.now();
};
```

### Graphics

Graphics callbacks are optional. If not provided or if initialization fails, the OS runs in headless mode where graphics operations succeed but produce no visual output.

#### `Module.graphicsInit(width: number, height: number): boolean`

Initialize the graphics subsystem.

- **width**: Framebuffer width in pixels
- **height**: Framebuffer height in pixels
- Returns: `true` if initialization succeeded (or headless mode is acceptable)

```javascript
// Browser example
Module.graphicsInit = function(width, height) {
  const canvas = document.getElementById('canvas');
  canvas.width = width;
  canvas.height = height;
  ctx = canvas.getContext('2d');
  imageData = ctx.createImageData(width, height);
  return true;
};
```

#### `Module.graphicsFlush(pixels: Uint32Array, width: number, height: number): void`

Render a framebuffer to the display.

- **pixels**: View into WASM memory containing pixel data (BGRA format, `0xAARRGGBB`)
- **width**: Framebuffer width
- **height**: Framebuffer height

The pixel data is a direct view into WASM heap memory. For browser canvas rendering, you may need to convert BGRA to RGBA:

```javascript
// Browser example
Module.graphicsFlush = function(pixels, width, height) {
  const data32 = new Uint32Array(imageData.data.buffer);
  data32.set(pixels.subarray(0, width * height));
  ctx.putImageData(imageData, 0, 0);
};

// Node.js SDL example (requires BGRA->RGBA conversion)
Module.graphicsFlush = function(pixels, width, height) {
  for (let i = 0; i < width * height; i++) {
    const pixel = pixels[i];
    pixelBuffer[i * 4 + 0] = (pixel >> 16) & 0xFF; // R
    pixelBuffer[i * 4 + 1] = (pixel >> 8) & 0xFF;  // G
    pixelBuffer[i * 4 + 2] = pixel & 0xFF;         // B
    pixelBuffer[i * 4 + 3] = (pixel >> 24) & 0xFF; // A
  }
  window.render(width, height, width * 4, 'rgba32', pixelBuffer);
};
```

#### `Module.graphicsCleanup(): void`

Clean up graphics resources. Called on shutdown.

```javascript
Module.graphicsCleanup = function() {
  // Release resources
};
```

### Filesystem

Filesystem callbacks are required when using the `wasm` filesystem backend. All paths are absolute, starting with `/`.

#### `Module.fsExists(path: string): string | null`

Check if a path exists and return its type.

- **path**: Absolute path (e.g., `/dir/file.txt`)
- Returns: `'file'`, `'dir'`, or `null` if not found

```javascript
Module.fsExists = function(path) {
  const entry = storage.get(normalizePath(path));
  return entry ? entry.type : null;
};
```

#### `Module.fsFileSize(path: string): number`

Get the size of a file in bytes.

- **path**: Absolute path to file
- Returns: File size in bytes, or `-1` if not found or not a file

```javascript
Module.fsFileSize = function(path) {
  const entry = storage.get(normalizePath(path));
  if (!entry || entry.type !== 'file') return -1;
  return entry.data ? entry.data.length : 0;
};
```

#### `Module.fsReadFile(path: string): Uint8Array | null`

Read a file's entire contents.

- **path**: Absolute path to file
- Returns: File contents as `Uint8Array`, or `null` if not found

```javascript
Module.fsReadFile = function(path) {
  const entry = storage.get(normalizePath(path));
  if (!entry || entry.type !== 'file') return null;
  return entry.data || new Uint8Array(0);
};
```

#### `Module.fsWriteFile(path: string, data: Uint8Array): boolean`

Write data to a file, creating it if necessary.

- **path**: Absolute path to file
- **data**: File contents to write
- Returns: `true` on success, `false` on failure

Note: Parent directories should be created automatically if they don't exist.

```javascript
Module.fsWriteFile = function(path, data) {
  ensureParentDirs(path);
  storage.set(normalizePath(path), { type: 'file', data: data });
  return true;
};
```

#### `Module.fsCreateFile(path: string): boolean`

Create an empty file.

- **path**: Absolute path for new file
- Returns: `true` on success, `false` if already exists or error

```javascript
Module.fsCreateFile = function(path) {
  if (storage.has(normalizePath(path))) return false;
  ensureParentDirs(path);
  storage.set(normalizePath(path), { type: 'file', data: new Uint8Array(0) });
  return true;
};
```

#### `Module.fsCreateDir(path: string): boolean`

Create a directory.

- **path**: Absolute path for new directory
- Returns: `true` on success, `false` if already exists or error

```javascript
Module.fsCreateDir = function(path) {
  if (storage.has(normalizePath(path))) return false;
  ensureParentDirs(path);
  storage.set(normalizePath(path), { type: 'dir', data: null });
  return true;
};
```

#### `Module.fsDeleteFile(path: string): boolean`

Delete a file.

- **path**: Absolute path to file
- Returns: `true` on success, `false` if not found or not a file

```javascript
Module.fsDeleteFile = function(path) {
  const entry = storage.get(normalizePath(path));
  if (!entry || entry.type !== 'file') return false;
  storage.delete(normalizePath(path));
  return true;
};
```

#### `Module.fsDeleteDir(path: string): boolean`

Delete an empty directory.

- **path**: Absolute path to directory
- Returns: `true` on success, `false` if not found, not a directory, or not empty

```javascript
Module.fsDeleteDir = function(path) {
  const entry = storage.get(normalizePath(path));
  if (!entry || entry.type !== 'dir') return false;
  if (hasChildren(path)) return false;  // Not empty
  storage.delete(normalizePath(path));
  return true;
};
```

### Lifecycle

#### `Module.exit(status: number): void`

Called when the OS exits.

- **status**: Exit status code (0 = success)
- Should terminate the process or clean up as appropriate

```javascript
Module.exit = function(status) {
  console.log('Exited with status', status);
  process.exit(status || 0);  // Node.js
};
```

#### `Module.onRuntimeInitialized(): void`

Called when the Emscripten runtime is ready, before `main()` is called.

- Use this to set up input handlers, display initialization messages, etc.

```javascript
Module.onRuntimeInitialized = function() {
  console.log('Runtime initialized');
  // Set up input handling, etc.
};
```

#### `Module.onAbort(what: string): void`

Called when the program aborts due to an error.

- **what**: Description of the abort reason

```javascript
Module.onAbort = function(what) {
  console.error('Program aborted:', what);
  process.exit(1);
};
```

## Emscripten Configuration

The following Emscripten settings are used in the build:

```javascript
Module.noInitialRun = false;    // Auto-run main()
Module.noExitRuntime = false;   // Allow clean exit
```

## Example: Minimal Implementation

Here's a minimal implementation for running tests in Node.js:

```javascript
const Module = {
  // Required
  print2: (text) => process.stdout.write(text),
  printErr: (text) => console.error(text),
  inputBuffer: [],
  time_get: () => Date.now(),
  exit: (status) => process.exit(status || 0),
  onRuntimeInitialized: () => {},
  onAbort: (what) => { console.error(what); process.exit(1); },

  // Graphics (headless)
  graphicsInit: () => true,
  graphicsFlush: () => {},
  graphicsCleanup: () => {},

  // Filesystem (in-memory)
  fsExists: (p) => storage.get(p)?.type || null,
  fsFileSize: (p) => storage.get(p)?.data?.length ?? -1,
  fsReadFile: (p) => storage.get(p)?.data || null,
  fsWriteFile: (p, d) => { storage.set(p, {type:'file',data:d}); return true; },
  fsCreateFile: (p) => { storage.set(p, {type:'file',data:new Uint8Array(0)}); return true; },
  fsCreateDir: (p) => { storage.set(p, {type:'dir',data:null}); return true; },
  fsDeleteFile: (p) => storage.delete(p),
  fsDeleteDir: (p) => storage.delete(p),

  noInitialRun: false,
  noExitRuntime: false,
};

require('./build-wasm/os.js')(Module);
```

## Reference Implementation

See `wasm-support.js` for a complete implementation including:

- In-memory filesystem with path normalization
- SDL graphics support for Node.js
- File sync with `fs-in/` and `fs-out/` directories

See `run-wasm.js` for how these are wired up to run the OS.

See `test-wasm.html` for a browser implementation with canvas rendering.
