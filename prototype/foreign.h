#ifndef OTIUM_PROTOTYPE_FOREIGN_H
#define OTIUM_PROTOTYPE_FOREIGN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*oti_foreign_callback)(void);

typedef union {
  int64_t signed_integer;
  uint64_t unsigned_integer;
  float single;
  double real;
  bool boolean;
  void* pointer;
  oti_foreign_callback callback;
} oti_foreign_slot;

#endif
