#ifndef OTOS_FILES_H
#define OTOS_FILES_H

#include <stddef.h>

/* Read-only files built into the kernel image, by absolute path. Each
 * one's data is followed by a NUL byte that length doesn't count. */
typedef struct embedded_file {
  const char* path;
  const unsigned char* data;
  size_t length;
} embedded_file;

extern const embedded_file embedded_files[];
extern const size_t embedded_file_count;

#endif
