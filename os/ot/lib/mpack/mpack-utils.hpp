#ifndef OT_SHARED_MPACK_UTILS_HPP
#define OT_SHARED_MPACK_UTILS_HPP

#include "ot/common.h"
#include "ot/lib/mpack/mpack.h"

// Function pointer for character output
// Returns: non-zero on success, 0 to abort printing
typedef int (*mpack_putchar_fn)(char ch);

// Pretty print msgpack data to the given putchar function
// Returns: 1 on success, 0 if putchar returned 0 (aborted)
int mpack_print(const char* data, size_t len, mpack_putchar_fn putchar_fn);

// Pretty print msgpack data to a scratch buffer
// Returns: number of bytes written (not including null terminator), or -1 if buffer too small
// The output will be null-terminated if it fits in the buffer
int mpack_sprint(const char* data, size_t data_len, char* buf, size_t buf_size);

#ifndef OT_POSIX
// Pretty print msgpack data using oputchar
// Returns: 1 on success, 0 if oputchar failed
int mpack_oprint(const char* data, size_t len);

// Pretty print msgpack data to ot_scratch_buffer
// Returns: number of bytes written (not including null terminator), or -1 if buffer too small
int mpack_scratch_print(const char* data, size_t data_len);
#endif

#endif  // OT_SHARED_MPACK_UTILS_HPP
