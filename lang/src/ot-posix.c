#define OT_INTERNAL
#include "otium.h"

#include <stdlib.h>
#include <time.h>

void* ot_host_alloc(size_t size) { return malloc(size); }

void* ot_host_realloc(void* memory, size_t size) { return realloc(memory, size); }

void ot_host_free(void* memory) { free(memory); }

uint64_t ot_platform_monotonic_ns(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

double ot_platform_current_second(void) {
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

bool ot_platform_read_file(const char* path, char** source, size_t* length) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) return false;
  size_t capacity = 4096;
  size_t used = 0;
  char* bytes = ot_host_alloc(capacity + 1);
  if (bytes == NULL) {
    fclose(file);
    return false;
  }
  for (;;) {
    if (used == capacity) {
      capacity *= 2;
      char* grown = ot_host_realloc(bytes, capacity + 1);
      if (grown == NULL) {
        ot_host_free(bytes);
        fclose(file);
        return false;
      }
      bytes = grown;
    }
    size_t got = fread(bytes + used, 1, capacity - used, file);
    used += got;
    if (got == 0) break;
  }
  bool ok = !ferror(file);
  fclose(file);
  if (!ok) {
    ot_host_free(bytes);
    return false;
  }
  bytes[used] = '\0';
  *source = bytes;
  *length = used;
  return true;
}

void ot_default_write(void* userdata, const char* bytes, size_t length) {
  FILE* file = userdata == NULL ? stdout : userdata;
  fwrite(bytes, 1, length, file);
}
