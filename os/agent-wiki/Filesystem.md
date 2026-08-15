Otium OS filesystem component provides platform-agnostic file operations through an IPC-based server with pluggable backends for RISC-V, WASM, and in-memory storage.

# Otium OS Filesystem Component

The Otium operating system implements a flexible filesystem abstraction layer that provides unified file operations across different platforms through an IPC-based server architecture with multiple backend implementations.

## Architecture Overview

The filesystem subsystem follows a client-server model where applications communicate with a dedicated filesystem server process through IPC (Inter-Process Communication):

```
┌─────────────────┐     IPC      ┌──────────────────────┐
│  Client Process │ ◄──────────► │  Filesystem Server   │
│  (shell, apps)  │              │  (impl-*.cpp)        │
└─────────────────┘              └──────────────────────┘
                                          │
                                          ▼
                                 ┌──────────────────────┐
                                 │  Backend (Disk/RAM)  │
                                 └──────────────────────┘
```

## Backend Implementations

Otium supports three filesystem backends, each optimized for different platforms:

### 1. Memory Backend (All Platforms)
- **File**: `impl-memory.cpp`
- **Description**: Simple in-memory filesystem for testing and development
- **Features**:
  - Volatile storage (data lost on shutdown)
  - Fast operations with no disk I/O
  - Ideal for unit testing and temporary data
- **Use case**: Default for non-FAT builds, testing environment

### 2. FAT32 Backend (RISC-V Only)
- **File**: `impl-fat.cpp`
- **Description**: Real FAT32 filesystem using the FatFs library
- **Features**:
  - Persistent storage on virtual disk
  - 8.3 filename format only (no long filename support)
  - Read/write operations with directory support
  - VirtIO block device driver integration
- **Components**:
  - `fatfs-diskio.cpp`: Bridges FatFs to Otium's disk abstraction
  - `virtio-disk.cpp`: VirtIO block device driver
  - FatFs library configuration in `ffconf.h`

### 3. WASM Backend (WebAssembly)
- **File**: `wasm-support.js`
- **Description**: JavaScript-based filesystem for browser/Node.js environments
- **Features**:
  - Case-insensitive paths (like FAT)
  - Preserves case for display
  - Synchronization with host filesystem:
    - `fs-in/`: Pre-loaded files before execution
    - `fs-out/`: Extracted files after execution
  - In-memory storage with host filesystem integration

## Configuration

Configure the filesystem backend during build:

```bash
# Memory backend (default for most platforms)
meson setup build -Dfilesystem_backend=memory

# FAT32 backend (RISC-V only)
meson setup build-riscv --cross-file=cross/riscv32.txt -Dfilesystem_backend=fat

# No filesystem
meson setup build -Dfilesystem_backend=none
```

The build system automatically selects appropriate defaults:
- RISC-V: `memory` (can use `fat`)
- WASM: `wasm`
- Other platforms: `none`

## IPC API

The filesystem service exposes these methods through IPC:

| Method | Description | Parameters |
|--------|-------------|------------|
| `open(path, flags)` | Open file | Path string, open flags |
| `read(handle, offset, length)` | Read data | File handle, byte offset, length |
| `write(handle, offset, data)` | Write data | File handle, byte offset, data |
| `close(handle)` | Close file | File handle |
| `create_file(path)` | Create new file | Path string |
| `delete_file(path)` | Delete file | Path string |
| `create_dir(path)` | Create directory | Path string |
| `delete_dir(path)` | Delete directory | Path string |

### Open Flags
```cpp
OPEN_READ     = 0x01  // Open for reading
OPEN_WRITE    = 0x02  // Open for writing
OPEN_CREATE   = 0x04  // Create if not exists
OPEN_TRUNCATE = 0x08  // Truncate existing file
```

## Testing Infrastructure

### Test Programs

Otium includes comprehensive filesystem tests:

1. **`test_filesystem`**: Tests the in-memory backend
   - Creates directories and files
   - Verifies read/write operations
   - Tests nested directories
   - Validates error handling

2. **`test_filesystem_fat`**: Tests FAT32 backend with disk images
   - Reads pre-existing files and verifies MD5 hashes
   - Creates new files and directories
   - Validates disk state after modifications

### Test Methodology

The testing framework (`test-snapshot.py`) implements several strategies:

#### Memory Backend Tests
- Creates filesystem operations in memory
- Verifies data integrity through read-back tests
- Tests edge cases and error conditions
- No persistent state between runs

#### FAT32 Backend Tests
1. **Baseline Image**: Uses `test-images/fat-baseline.img.gz`
   - 128MB FAT32 image (compressed to ~128KB)
   - Contains test file `LOREM8K.TXT` with known MD5
   - Large enough to ensure >65525 clusters for FAT32

2. **Test Process**:
   - Decompresses baseline image to `disk.fat.img`
   - Runs test (may modify disk)
   - Computes SHA256 of resulting disk
   - Includes hash in snapshot for verification

3. **Disk Management**:
   ```bash
   # Create test image
   dd if=/dev/zero of=test-images/fat-baseline.img bs=1M count=128
   mformat -F -v OTIUMTEST -i test-images/fat-baseline.img ::
   mcopy -i test-images/fat-baseline.img test-images/LOREM8K.TXT ::
   gzip -9 test-images/fat-baseline.img
   ```

#### WASM Backend Tests
- Uses Node.js filesystem simulation
- Tests synchronization between in-memory and host filesystem
- Validates case-insensitive path handling

### Test Implementation Details

Tests use a client-server model:

```cpp
// Server process
Process *fs_server = process_create("filesystem", proc_filesystem, nullptr, false);

// Test client process
Process *test_client = process_create("fs_test_client", proc_filesystem_test_client, nullptr, false);
```

The test client performs operations like:
- Creating directories: `/testdir`, `/testdir/subdir`
- Writing files with known content
- Reading files and verifying content/size
- Testing error conditions (file not found, etc.)

## Development Workflow

### RISC-V with FAT Backend

```bash
# Build with FAT support
meson setup build-riscv --cross-file=cross/riscv32.txt -Dfilesystem_backend=fat
meson compile -C build-riscv

# Run with automatic disk image management
tools/run-qemu-riscv.sh build-riscv
# - Creates disk.fat.img from fs-in/ contents
# - Runs QEMU with the disk image
# - Extracts modified disk to fs-out/ after exit

# Or use a custom disk image
tools/run-qemu-riscv.sh build-riscv /path/to/custom.img
```

### WASM Development

```bash
# Build WASM version
meson setup build-wasm --cross-file=cross/wasm32.txt
meson compile -C build-wasm

# Run with Node.js
node run-wasm.js build-wasm/ot.wasm
# - Loads files from fs-in/
# - Saves modified files to fs-out/
```

## Platform-Specific Considerations

### RISC-V
- Supports both memory and FAT backends
- FAT backend requires proper disk image (>65525 clusters for FAT32)
- VirtIO disk driver handles block device operations
- Limited to 8.3 filenames with FAT

### WASM
- JavaScript-based filesystem in `wasm-support.js`
- Case-insensitive paths but preserves case for display
- Automatic sync with host filesystem directories
- No size limitations beyond available memory

### In-Memory
- Platform-agnostic implementation
- No persistence between runs
- Useful for testing and temporary data
- No filesystem size limits beyond available RAM

## Adding New Backends

To implement a new filesystem backend:

1. Create `ot/user/fs/impl-<name>.cpp`
2. Inherit from `FilesystemServerBase`
3. Implement all required methods
4. Update build configuration:
   - Add to `meson.build` backend selection
   - Add option to `meson_options.txt`
   - Add constant to `ot/config.h.meson.in`
5. Add appropriate tests

The modular design allows easy addition of new backends while maintaining consistent API across all platforms.