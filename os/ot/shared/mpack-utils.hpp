#ifndef OT_SHARED_MPACK_UTILS_HPP
#define OT_SHARED_MPACK_UTILS_HPP

#include "ot/common.h"
#include "ot/shared/mpack.h"

// Function pointer for character output
// Returns: non-zero on success, 0 to abort printing
typedef int (*mpack_putchar_fn)(char ch);

// Pretty print msgpack data to the given putchar function
// Returns: 1 on success, 0 if putchar returned 0 (aborted)
int mpack_print(const char* data, size_t len, mpack_putchar_fn putchar_fn);

#ifndef OT_TEST
// Pretty print msgpack data using oputchar
// Returns: 1 on success, 0 if oputchar failed
int mpack_oprint(const char* data, size_t len);
#endif

#endif  // OT_SHARED_MPACK_UTILS_HPP
