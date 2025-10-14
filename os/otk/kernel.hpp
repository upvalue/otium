#ifndef OTK_KERNEL_HPP
#define OTK_KERNEL_HPP

#include "otcommon.h"

#define PAGE_SIZE 4096

#define PANIC(fmt, ...)                                                        \
  do {                                                                         \
    oprintf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);     \
    while (1) {                                                                \
    }                                                                          \
  } while (0)

#define TRACE(fmt, ...)                                                        \
  do {                                                                         \
    oprintf("TRACE: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);     \
  } while (0)

#ifdef OT_TRACE_MEM
#define TRACE_MEM(fmt, ...)                                                    \
  do {                                                                         \
    oprintf("TRACE_MEM: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
  } while (0)
#else
#define TRACE_MEM(fmt, ...)
#endif

struct sbiret {
  long error;
  long value;
};

void kernel_common(void);
void wfi(void);

// memory management
void *page_allocate_impl(char *begin, char *end, char **next, size_t page_size);

void *page_allocate(size_t n);

extern "C" char *__free_ram, *__free_ram_end;

#endif