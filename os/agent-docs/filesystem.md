# Filesystem Subsystem

The OS provides a filesystem abstraction through an IPC-based server process. Multiple backend implementations are available.

## Architecture

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

## Backends

Configure with `-Dfilesystem_backend=<backend>`:

| Backend  | Description | Platform Support |
|----------|-------------|------------------|
| `none`   | No filesystem | All |
| `memory` | In-memory filesystem (volatile) | All |
| `fat`    | FAT32 via FatFs library | RISC-V only |

### Memory Backend (`impl-memory.cpp`)

Simple in-memory filesystem for testing. Data is lost on shutdown.

### FAT Backend (`impl-fat.cpp`)

Real FAT32 filesystem using the [FatFs library](http://elm-chan.org/fsw/ff/00index_e.html).

**Features:**
- FAT32 support (requires >65525 clusters)
- 8.3 filenames only (no long filename support)
- Read/write operations
- Directory creation/deletion

**Components:**
- `ot/vendor/fatfs/` - FatFs library (ff.c, ff.h, diskio.h)
- `ot/vendor/fatfs/ffconf.h` - OS-specific configuration
- `ot/user/fs/fatfs-diskio.cpp` - Disk I/O glue layer
- `ot/user/fs/impl-fat.cpp` - Filesystem server implementation
- `ot/user/fs/virtio-disk.cpp` - VirtIO block device driver

**Disk I/O Layer:**

The `fatfs-diskio.cpp` file bridges FatFs to our `Disk` abstract class:

```cpp
void fatfs_set_disk(Disk *disk);  // Set disk before mounting

// FatFs diskio interface implementations:
DSTATUS disk_initialize(BYTE pdrv);
DSTATUS disk_status(BYTE pdrv);
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count);
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count);
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);
```

**Configuration (`ffconf.h`):**

Key settings for this OS:
```c
#define FF_FS_READONLY    0      // Read/Write support
#define FF_USE_LFN        0      // No long filenames (8.3 only)
#define FF_CODE_PAGE      437    // US code page
#define FF_VOLUMES        1      // Single volume
#define FF_MIN_SS         512
#define FF_MAX_SS         512    // Fixed 512-byte sectors
#define FF_FS_NORTC       1      // No RTC (fixed timestamp)
```

## IPC API

Defined in `services/filesystem.yml`. See generated client: `ot/user/gen/filesystem-client.hpp`

### Methods

| Method | Description |
|--------|-------------|
| `open(path, flags)` | Open file, returns handle |
| `read(handle, offset, length)` | Read data (via comm page) |
| `write(handle, offset, data)` | Write data (via comm page) |
| `close(handle)` | Close file handle |
| `create_file(path)` | Create new file |
| `delete_file(path)` | Delete file |
| `create_dir(path)` | Create directory |
| `delete_dir(path)` | Delete empty directory |

### Open Flags

```cpp
OPEN_READ     = 0x01  // Open for reading
OPEN_WRITE    = 0x02  // Open for writing
OPEN_CREATE   = 0x04  // Create if not exists
OPEN_TRUNCATE = 0x08  // Truncate existing file
```

### Usage Example

```cpp
#include "ot/user/gen/filesystem-client.hpp"

FilesystemClient fs(filesystem_pid);

// Write a file
auto handle = fs.open("/hello.txt", OPEN_WRITE | OPEN_CREATE);
if (handle.is_ok()) {
  fs.write(handle.value(), 0, "Hello, World!");
  fs.close(handle.value());
}

// Read a file
handle = fs.open("/hello.txt", OPEN_READ);
if (handle.is_ok()) {
  auto result = fs.read(handle.value(), 0, 1024);
  // Data is in comm page as msgpack binary
  fs.close(handle.value());
}
```

## Testing

### Test Programs

- `test_filesystem` - Tests in-memory backend
- `test_filesystem_fat` - Tests FAT32 backend with disk image

### FAT Filesystem Test

The FAT test (`KERNEL_PROG_TEST_FILESYSTEM_FAT`) performs:
1. Reads `/LOREM8K.TXT` (8KB file) from disk
2. Computes MD5 hash and verifies against known value
3. Creates and writes `/TEST.TXT`
4. Reads back and verifies content

**Test Image:**

Location: `test-images/fat-baseline.img.gz` (128MB FAT32, gzip compressed to ~128KB)

Contains:
- `LOREM8K.TXT` - 8192 bytes, MD5: `2b7dd37b7729fc91446f0ca09670ed3d`

The test runner automatically decompresses this to `disk.fat.img` before each test run.

**Creating/Recreating the Test Image:**

```bash
# Create 128MB FAT32 image (must be large enough for >65525 clusters)
dd if=/dev/zero of=test-images/fat-baseline.img bs=1M count=128
mformat -F -v OTIUMTEST -i test-images/fat-baseline.img ::

# Add test file
mcopy -i test-images/fat-baseline.img test-images/LOREM8K.TXT ::

# Compress for storage (128MB -> ~128KB)
gzip -9 test-images/fat-baseline.img
```

**Why 128MB?**

FAT32 is determined by cluster count, not filesystem label. FatFs classifies:
- ≤4085 clusters → FAT12
- ≤65525 clusters → FAT16
- >65525 clusters → FAT32

A 4MB image only has ~8000 clusters, which FatFs treats as FAT16. But the mtools-created image has FAT32 BPB fields (root entries = 0), causing mount failure. Using 128MB ensures proper FAT32 detection.

### Snapshot Testing

The test runner (`test-snapshot.py`) handles FAT tests specially:

1. Decompresses `test-images/fat-baseline.img.gz` to `disk.fat.img` before test
2. Runs test (which may modify the disk)
3. Computes SHA256 of resulting disk image
4. Includes `TEST: IMAGE_SHA256=...` in snapshot for verification

This ensures both the test output AND the resulting disk state are verified.

## Running with FAT Filesystem

### Development Workflow

The run script automatically manages disk images:

```bash
# Build with FAT backend
meson setup build-riscv --cross-file=cross/riscv32.txt -Dfilesystem_backend=fat
meson compile -C build-riscv

# Run in QEMU
# - Creates disk.fat.img from fs-in/ contents before running
# - Extracts disk contents to fs-out/ after QEMU exits
tools/run-qemu-riscv.sh build-riscv
```

**Directory structure:**
- `fs-in/` - Files to include on disk image (committed to git)
- `fs-out/` - Extracted disk contents after run (gitignored)
- `disk.fat.img` - Generated 128MB FAT32 image (gitignored)

### Using a Specific Disk Image

To use a custom disk image (skips create/extract):

```bash
tools/run-qemu-riscv.sh build-riscv /path/to/custom.img
```

## Adding New Backends

1. Create `ot/user/fs/impl-<name>.cpp`
2. Implement `FatFilesystemServer` (or similar) inheriting from `FilesystemServerBase`
3. Add to `meson.build` filesystem backend selection
4. Add option to `meson_options.txt`
5. Add constant to `ot/config.h.meson.in`

## Utilities

### MD5 Library

`ot/lib/md5.hpp` / `ot/lib/md5.cpp` provides MD5 hashing:

```cpp
#include "ot/lib/md5.hpp"

MD5_CTX ctx;
md5_init(&ctx);
md5_update(&ctx, data, length);
uint8_t digest[16];
md5_final(digest, &ctx);

char hex[33];
md5_digest_to_hex(digest, hex);
```
