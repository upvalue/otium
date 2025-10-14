#ifndef OTK_KERNEL_HPP
#define OTK_KERNEL_HPP

#include "otcommon.h"

#define PANIC(fmt, ...)                                                        \
  do {                                                                         \
    printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);      \
    while (1) {                                                                \
    }                                                                          \
  } while (0)

struct sbiret {
  long error;
  long value;
};

void kernel_common(void);
void wfi(void);

#endif