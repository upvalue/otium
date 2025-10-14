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

void kernel_common(void);
void wfi(void);

// memory management
void *page_allocate_impl(char *begin, char *end, char **next, size_t page_size);
void *page_allocate(size_t n);
extern "C" char __free_ram[], __free_ram_end[];

// process management
#define PROCS_MAX 8

enum ProcessState { UNUSED, RUNNABLE };

struct ProcessContext {
  uintptr_t return_addr;
  uintptr_t stack_ptr;

  uint32_t s0;
  uint32_t s1;
  uint32_t s2;
  uint32_t s3;
  uint32_t s4;
  uint32_t s5;
  uint32_t s6;
  uint32_t s7;
  uint32_t s8;
  uint32_t s9;
  uint32_t s10;
  uint32_t s11;
};

struct Process {
  char name[32];
  uint32_t pid;
  ProcessState state;
  ProcessContext ctx;
  uint8_t stack[8192];
};

Process *process_create_impl(Process *table, uint32_t max_procs,
                             const char *name, uint32_t pc);
Process *process_create(const char *name, uintptr_t pc);
Process *process_next_runnable(void);

extern Process *idle_proc, *current_proc;
extern Process procs[PROCS_MAX];

extern "C" void switch_context(uintptr_t *prev_sp, uintptr_t *next_sp);
void yield(void);

#endif